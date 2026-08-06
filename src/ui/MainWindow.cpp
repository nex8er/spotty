/**
 * \file MainWindow.cpp
 * \brief Реализация spotty::MainWindow.
 */
#include "MainWindow.h"

#include "Formatting.h"
#include "InterfaceBar.h"
#include "SendBar.h"
#include "dialogs/InterfaceSettingsDialog.h"
#include "dialogs/InterfaceSettingsPanel.h"
#include "dialogs/SettingsDialog.h"
#include "panels/GeneratorPanel.h"
#include "panels/LoggingPanel.h"
#include "panels/MacrosPanel.h"
#include "panels/SearchPanel.h"
#include "theme/MdiIcons.h"

#include <HistoryStore.h>
#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <Session.h>
#include <settings/Paths.h>
#include <settings/SettingsStore.h>

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr int kPanelGlyphSize = 20;
constexpr int kToolGlyphSize = 18;

/**
 * \brief Ширины полей строки состояния (RX/TX, скорость, ошибки), в знаках моноширинного
 *        шрифта.
 *
 * Значение каждого поля дополняется пробелами до этой ширины, поэтому смена «340 B» на
 * «1.2 KiB» не сдвигает то, что стоит правее, — вплоть до #kStatsReservedWidth.
 */
constexpr int kStatsValueWidth = 10;   // "9999.9 GiB"
constexpr int kStatsRateWidth = 12;    // "9999.9 GiB/s"
constexpr int kStatsErrorsWidth = 13;  // "9999 error(s)"

/// \brief Пустой резерв в конце строки состояния под будущий индикатор.
constexpr int kStatsReservedWidth = 12;

// Состояние окна, а не пользовательские настройки: сюда spotty::AppSettings не лезет.
constexpr auto kKeyGeometry = "window/geometry";
// Ключ сменился вместе с раскладкой: панель переехала налево, и сохранённые прежде
// размеры относились к обратному порядку виджетов. Восстановление старого состояния
// растянуло бы панель на всё окно.
constexpr auto kKeySplitter = "window/mainSplitter";
constexpr auto kKeyPanelIndex = "window/panelIndex";
constexpr auto kKeyInterface = "session/interfaceId";

/// \brief Значки правых панелей в порядке их страниц в стопке.
constexpr char32_t kPanelGlyphs[] = {
    mdi::Flash,               // Макросы
    mdi::RecordCircleOutline, // Логирование
    mdi::Magnify,             // Поиск
    mdi::ShuffleVariant,      // Генератор
};

/// \brief Подсказки к значкам панелей, в том же порядке.
const char *const kPanelTitles[] = {
    QT_TRANSLATE_NOOP("spotty::MainWindow", "Macros"),
    QT_TRANSLATE_NOOP("spotty::MainWindow", "Logging"),
    QT_TRANSLATE_NOOP("spotty::MainWindow", "Search"),
    QT_TRANSLATE_NOOP("spotty::MainWindow", "Generator"),
};

/// \brief Кодировка приёма по имени из настроек.
QStringConverter::Encoding encodingFromName(const QString &name)
{
    return name == QLatin1String("latin1") ? QStringConverter::Latin1
                                           : QStringConverter::Utf8;
}

} // namespace

MainWindow::MainWindow(const AppContext &context, QWidget *parent)
    : QMainWindow(parent)
    , m_context(context)
    , m_settings(AppSettings::load(*context.settings))
{
    setWindowTitle(QStringLiteral("Spotty %1").arg(QLatin1String(SPOTTY_VERSION)));

    buildUi();
    buildMenus();
    restoreWindowState();
    applySettings();

    if (m_context.theme)
        connect(m_context.theme, &ThemeManager::themeChanged, this, [this] { updateIcons(); });
    updateIcons();

    // Список интерфейсов всегда стартует с «не выбрано»: выбор теперь сам открывает канал
    // (обработчик interfaceSelected ниже), и показывать выбранным то, что на самом деле
    // закрыто, было бы враньём в интерфейсе. Единственное исключение — включённое
    // автооткрытие последнего интерфейса: тогда выбор и открытие происходят вместе.
    const QString interfaceId =
        m_context.settings->value(QLatin1String(kKeyInterface)).toString();
    if (m_context.session && !interfaceId.isEmpty() && m_settings.autoOpenLastInterface) {
        m_interfaceBar->setCurrentInterfaceId(interfaceId);
        m_context.session->setInterfaceId(interfaceId);
        m_context.session->open();
    }
}

QWidget *MainWindow::makeCard(QWidget *content)
{
    // Каждый блок — отдельная карточка с рамкой и скруглением. Оформление задаётся
    // единственным правилом QSS по objectName, поэтому все блоки заведомо выглядят
    // одинаково и не расходятся при правках.
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(content);

    return card;
}

void MainWindow::buildUi()
{
    m_interfaceBar = new InterfaceBar(m_context, this);
    m_terminal = new TerminalView(this);
    m_terminal->setObjectName(QStringLiteral("terminalView"));
    m_terminal->setBuffer(m_context.session ? m_context.session->buffer() : nullptr);
    m_terminal->setThemeManager(m_context.theme);

    m_sendBar = new SendBar(m_context.history, this);

    // Полоса просмотра лога: пока она скрыта, ничто о ней не напоминает, а при открытии
    // файла она сразу отвечает на вопрос «что я сейчас вижу и как вернуться».
    m_logViewBar = new QWidget(this);
    m_logViewBar->setObjectName(QStringLiteral("terminalToolbar"));
    m_logViewLabel = new QLabel(m_logViewBar);
    auto *backButton = new QPushButton(tr("Back to live output"), m_logViewBar);
    auto *logBarLayout = new QHBoxLayout(m_logViewBar);
    logBarLayout->setContentsMargins(8, 4, 8, 4);
    logBarLayout->addWidget(m_logViewLabel, 1);
    logBarLayout->addWidget(backButton);
    m_logViewBar->hide();
    connect(backButton, &QPushButton::clicked, this, &MainWindow::returnToLiveView);

    // --- Блок терминала: кнопки показа и сам вывод одной карточкой -------------------
    //
    // Кнопки управляют именно этим выводом, поэтому живут внутри его рамки, а не отдельной
    // полосой через всё окно: так видно, к чему они относятся.
    auto *terminalBlock = new QWidget(this);
    auto *terminalLayout = new QVBoxLayout(terminalBlock);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->setSpacing(0);
    terminalLayout->addWidget(buildTerminalToolbar());
    terminalLayout->addWidget(m_logViewBar);
    terminalLayout->addWidget(m_terminal, 1);

    // --- Правая колонка: интерфейс, терминал, отправка --------------------------------
    //
    // Поля и промежутки берутся из ThemeMetrics, а не задаются числом: macOS расставляет
    // блоки просторнее Windows, и одно значение на обе системы выглядит тесно на одной из
    // них.
    const ThemeMetrics &metrics = ThemeManager::metrics();

    auto *rightColumn = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(metrics.gap, metrics.gap, metrics.gap, metrics.gap);
    rightLayout->setSpacing(metrics.gap);
    rightLayout->addWidget(makeCard(m_interfaceBar));
    rightLayout->addWidget(makeCard(terminalBlock), 1);
    rightLayout->addWidget(makeCard(m_sendBar));

    // --- Панель слева во всю высоту ---------------------------------------------------
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(buildSidePanel());
    m_splitter->addWidget(rightColumn);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    // Панель должна полностью схлопываться: при работе с длинными строками терминалу
    // нужна вся ширина окна.
    m_splitter->setChildrenCollapsible(true);
    // Захват шире рисуемой линии. Прежний однопиксельный разделитель поймать курсором
    // почти невозможно: промах не делает ничего, и разделитель выглядит неработающим.
    m_splitter->setHandleWidth(metrics.splitterHandle);

    setCentralWidget(m_splitter);

    m_statsLabel = new QLabel(this);
    // Моноширинный шрифт — иначе выравнивание пробелами в updateStatistics() ничего не
    // даёт: у пропорционального шрифта одинаковое число символов занимает разную ширину.
    m_statsLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_linesLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_linesLabel);
    statusBar()->addPermanentWidget(m_statsLabel);

    // --- Связывание ------------------------------------------------------------------

    connect(m_interfaceBar, &InterfaceBar::interfaceSelected, this, [this](const QString &id) {
        m_context.settings->setValue(QLatin1String(kKeyInterface), id);
        if (!m_context.session)
            return;

        m_context.session->setInterfaceId(id);

        // Выбор в выпадающем списке — уже осознанное решение пользователя: он назвал
        // порт, с которым хочет работать, и второй клик по кнопке открытия был бы лишним
        // повторением того же намерения. «Не выбрано» (пустая строка) сюда не подпадает —
        // им закрывают канал, а не открывают ничего.
        //
        // Это не противоречит выключенному по умолчанию автооткрытию при запуске
        // (session/autoOpen): там программа решает сама, без прямого действия человека
        // прямо сейчас, и там DTR-сброс платы или перехват порта у другой программы был
        // бы сюрпризом. Здесь выбор — сам по себе прямое действие.
        if (!id.isEmpty())
            m_context.session->open();
    });

    connect(m_interfaceBar, &InterfaceBar::toggleOpenRequested,
            this, &MainWindow::toggleConnection);
    connect(m_interfaceBar, &InterfaceBar::settingsRequested, this, [this](const QString &id) {
        showInterfaceSettings(id);
    });

    connect(m_sendBar, &SendBar::sendRequested, this, [this](const QByteArray &data) {
        if (m_context.session)
            m_context.session->send(data);
    });

    connect(m_sendBar, &SendBar::optionsChanged, this, [this] {
        m_settings.sendFormat = int(m_sendBar->format());
        m_settings.sendTermination = int(m_sendBar->termination());
        m_settings.save(*m_context.settings);
    });

    connect(m_terminal, &TerminalView::followTailChanged, this, [this](bool following) {
        m_followButton->setChecked(following);
    });

    if (m_context.session) {
        connect(m_context.session, &Session::stateChanged,
                this, &MainWindow::applyChannelState);
        connect(m_context.session, &Session::statisticsChanged,
                this, &MainWindow::updateStatistics);
        connect(m_context.session, &Session::controlLinesChanged,
                this, &MainWindow::updateControlLines);
        connect(m_context.session, &Session::errorOccurred, this, [this](const QString &message) {
            statusBar()->showMessage(message, 8000);
            m_interfaceBar->flagError(message);
        });
        connect(m_context.session, &Session::requiredSettingsMissing, this,
                [this](const QString &interfaceId, const QStringList &missingFieldKeys) {
            statusBar()->showMessage(
                tr("Fill in the required settings to open this interface."), 8000);
            showInterfaceSettings(interfaceId, missingFieldKeys);
        });
    }

    // --- Панели ----------------------------------------------------------------------

    const auto sendFromPanel = [this](const QByteArray &data) {
        if (m_context.session)
            m_context.session->send(data);
    };
    connect(m_macrosPanel, &MacrosPanel::sendRequested, this, sendFromPanel);
    connect(m_generatorPanel, &GeneratorPanel::sendRequested, this, sendFromPanel);

    const auto showStatus = [this](const QString &message) {
        statusBar()->showMessage(message, 8000);
    };
    connect(m_macrosPanel, &MacrosPanel::statusMessage, this, showStatus);
    connect(m_loggingPanel, &LoggingPanel::statusMessage, this, showStatus);

    connect(m_generatorPanel, &GeneratorPanel::pushToSendBarRequested, this,
            [this](const QString &text, int format) {
                m_sendBar->setFormat(DataCodec::Format(format));
                m_sendBar->setText(text);
                m_sendBar->focusInput();
            });

    connect(m_loggingPanel, &LoggingPanel::logFileRequested, this, &MainWindow::showLogFile);
    connect(m_loggingPanel, &LoggingPanel::optionsChanged, this,
            [this](bool filterAnsi, bool includeTx) {
                m_settings.logFilterAnsi = filterAnsi;
                m_settings.logIncludeTx = includeTx;
                m_settings.save(*m_context.settings);
            });

    connect(m_searchPanel, &SearchPanel::searchChanged, this,
            [this](const QString &pattern, bool regex, bool caseSensitive, bool wholeWords) {
                m_terminal->setSearchPattern(pattern, regex, caseSensitive, wholeWords);
            });
    connect(m_searchPanel, &SearchPanel::filterToggled,
            m_terminal, &TerminalView::setFilterEnabled);
    connect(m_searchPanel, &SearchPanel::findNextRequested,
            m_terminal, &TerminalView::findNext);
    connect(m_searchPanel, &SearchPanel::findPreviousRequested,
            m_terminal, &TerminalView::findPrevious);
    connect(m_searchPanel, &SearchPanel::highlightRulesChanged,
            m_terminal, &TerminalView::setHighlightRules);
    connect(m_terminal, &TerminalView::matchCountChanged,
            m_searchPanel, &SearchPanel::setMatchCount);

    // Правила, прочитанные панелью из настроек в её конструкторе, до подключения выше не
    // дошли бы: сигнала тогда ещё некому было слушать.
    m_terminal->setHighlightRules(m_searchPanel->highlightRules());

    applyChannelState(ChannelState::Closed, {});
    updateStatistics();
    updateControlLines({});
    resize(1180, 720);
}

QWidget *MainWindow::buildTerminalToolbar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("terminalToolbar"));

    const auto makeButton = [this, bar](const QString &tip, bool checkable) {
        auto *button = new QToolButton(bar);
        button->setAutoRaise(true);
        button->setCheckable(checkable);
        button->setToolTip(tip);
        button->setIconSize(QSize(kToolGlyphSize, kToolGlyphSize));
        return button;
    };

    m_hexButton = makeButton(tr("Show data as a hexadecimal dump"), true);
    m_timestampButton = makeButton(tr("Show timestamps"), true);
    m_directionButton = makeButton(tr("Show transmit and receive marks"), true);
    m_clearButton = makeButton(tr("Clear the terminal"), false);
    m_followButton = makeButton(tr("Follow output"), true);
    m_followButton->setChecked(true);

    connect(m_hexButton, &QToolButton::toggled, this, [this](bool hex) {
        m_terminal->setViewMode(hex ? TerminalView::ViewMode::Hex
                                    : TerminalView::ViewMode::Text);
        m_settings.viewMode = hex ? QStringLiteral("hex") : QStringLiteral("text");
        m_settings.save(*m_context.settings);
    });
    connect(m_timestampButton, &QToolButton::toggled, this, [this](bool show) {
        m_terminal->setShowTimestamps(show);
        m_settings.showTimestamps = show;
        m_settings.save(*m_context.settings);
    });
    connect(m_directionButton, &QToolButton::toggled, this, [this](bool show) {
        m_terminal->setShowDirection(show);
        m_settings.showDirection = show;
        m_settings.save(*m_context.settings);
    });
    connect(m_clearButton, &QToolButton::clicked, this, [this] {
        if (m_context.session)
            m_context.session->buffer()->clear();
    });
    connect(m_followButton, &QToolButton::toggled,
            m_terminal, &TerminalView::setFollowTail);

    // Разделитель, а не просто промежуток: слева переключатели того, как показан вывод,
    // справа действия над ним. Промежутка в 8 px для этого мало — рядом с промежутком в
    // 2 px между кнопками он читается как случайный, и все пять кнопок выглядят одной
    // группой.
    //
    // Обычный QFrame::VLine здесь не годится: он рисуется палитрой стиля, а не цветом
    // темы, и на тёмной теме получается вдавленная двухцветная канавка вместо линии.
    auto *separator = new QFrame(bar);
    separator->setObjectName(QStringLiteral("toolSeparator"));
    separator->setFixedWidth(1);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(2);
    layout->addWidget(m_hexButton);
    layout->addWidget(m_timestampButton);
    layout->addWidget(m_directionButton);
    layout->addSpacing(6);
    layout->addWidget(separator);
    layout->addSpacing(6);
    layout->addWidget(m_clearButton);
    layout->addWidget(m_followButton);
    layout->addStretch(1);

    return bar;
}

QWidget *MainWindow::buildSidePanel()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("sidePanel"));
    panel->setMinimumWidth(300);

    // Вертикальная рейка значков вместо полосы вкладок: она читается при любой ширине
    // панели и не вносит в оформление лишних рамок.
    auto *rail = new QWidget(panel);
    // Линия между рейкой и содержимым панели (правило #panelRail в QSS): без неё при
    // узкой панели столбец значков и её содержимое сливаются в один столбец кнопок.
    rail->setObjectName(QStringLiteral("panelRail"));
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(4, 6, 4, 6);
    railLayout->setSpacing(4);

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);

    m_panelStack = new QStackedWidget(panel);

    m_macrosPanel = new MacrosPanel(m_context, m_panelStack);
    m_loggingPanel = new LoggingPanel(m_context, m_panelStack);
    m_searchPanel = new SearchPanel(m_context, m_panelStack);
    m_generatorPanel = new GeneratorPanel(m_context, m_panelStack);

    m_panelStack->addWidget(m_macrosPanel);
    m_panelStack->addWidget(m_loggingPanel);
    m_panelStack->addWidget(m_searchPanel);
    m_panelStack->addWidget(m_generatorPanel);

    for (int index = 0; index < int(std::size(kPanelGlyphs)); ++index) {
        auto *button = new QToolButton(rail);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolTip(tr(kPanelTitles[index]));
        button->setIconSize(QSize(kPanelGlyphSize, kPanelGlyphSize));
        group->addButton(button, index);
        railLayout->addWidget(button);
        m_panelButtons.append(button);
    }

    railLayout->addStretch(1);

    // Настройки — не панель, а действие, поэтому вне группы переключателей: растяжка
    // отделяет её от них и прижимает к низу рейки.
    m_settingsRailButton = new QToolButton(rail);
    m_settingsRailButton->setAutoRaise(true);
    m_settingsRailButton->setToolTip(tr("Settings"));
    m_settingsRailButton->setIconSize(QSize(kPanelGlyphSize, kPanelGlyphSize));
    railLayout->addWidget(m_settingsRailButton);
    connect(m_settingsRailButton, &QToolButton::clicked, this, &MainWindow::showSettingsDialog);

    connect(group, &QButtonGroup::idClicked, m_panelStack, &QStackedWidget::setCurrentIndex);

    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(rail);
    layout->addWidget(m_panelStack, 1);

    if (!m_panelButtons.isEmpty())
        m_panelButtons.first()->setChecked(true);

    return panel;
}

void MainWindow::buildMenus()
{
    // На macOS пункты «Settings» и «Quit» переезжают в меню приложения, а «About» — тоже
    // туда, поэтому меню «File» и «Help» в строке меню не показываются. Это штатное
    // поведение Qt, а не потерянные пункты.
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *preferences = fileMenu->addAction(tr("&Settings..."), this,
                                               &MainWindow::showSettingsDialog);
    preferences->setShortcut(QKeySequence::Preferences);
    preferences->setMenuRole(QAction::PreferencesRole);
    fileMenu->addSeparator();
    QAction *quit = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu *interfaceMenu = menuBar()->addMenu(tr("&Interface"));
    m_actions.insert(QStringLiteral("interface.toggle"),
                     interfaceMenu->addAction(tr("&Open / Close"), this,
                                              &MainWindow::toggleConnection));
    interfaceMenu->addSeparator();

    // Ручное управление линиями: у многих плат DTR и RTS заведены на сброс и загрузчик,
    // и дёрнуть их вручную — обычная отладочная операция.
    interfaceMenu->addAction(tr("Toggle &DTR"), this, [this] {
        if (m_context.session) {
            const bool current =
                m_context.session->controlLines().value(QStringLiteral("DTR")).toBool();
            m_context.session->setControlLine(QStringLiteral("DTR"), !current);
        }
    });
    interfaceMenu->addAction(tr("Toggle &RTS"), this, [this] {
        if (m_context.session) {
            const bool current =
                m_context.session->controlLines().value(QStringLiteral("RTS")).toBool();
            m_context.session->setControlLine(QStringLiteral("RTS"), !current);
        }
    });
    interfaceMenu->addAction(tr("Send &Break"), this, [this] {
        if (m_context.session)
            m_context.session->sendBreak();
    });

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

    QAction *hexAction = viewMenu->addAction(tr("&Hexadecimal dump"));
    hexAction->setCheckable(true);
    connect(hexAction, &QAction::toggled, m_hexButton, &QToolButton::setChecked);
    connect(m_hexButton, &QToolButton::toggled, hexAction, &QAction::setChecked);
    m_actions.insert(QStringLiteral("terminal.hex"), hexAction);

    QAction *timestampAction = viewMenu->addAction(tr("&Timestamps"));
    timestampAction->setCheckable(true);
    connect(timestampAction, &QAction::toggled, m_timestampButton, &QToolButton::setChecked);
    connect(m_timestampButton, &QToolButton::toggled, timestampAction, &QAction::setChecked);
    m_actions.insert(QStringLiteral("terminal.timestamps"), timestampAction);

    m_actions.insert(QStringLiteral("terminal.clear"),
                     viewMenu->addAction(tr("C&lear terminal"), this, [this] {
                         if (m_context.session)
                             m_context.session->buffer()->clear();
                     }));

    viewMenu->addSeparator();

    m_actions.insert(QStringLiteral("search.focus"),
                     viewMenu->addAction(tr("&Find..."), this, [this] {
                         m_panelStack->setCurrentIndex(2);
                         if (m_panelButtons.size() > 2)
                             m_panelButtons.at(2)->setChecked(true);
                         m_searchPanel->focusSearch();
                     }));

    m_actions.insert(QStringLiteral("send.focus"),
                     viewMenu->addAction(tr("Focus &send bar"), this,
                                         [this] { m_sendBar->focusInput(); }));

    m_actions.insert(QStringLiteral("logging.toggle"),
                     viewMenu->addAction(tr("Start / stop &recording"), this, [this] {
                         m_panelStack->setCurrentIndex(1);
                         if (m_panelButtons.size() > 1)
                             m_panelButtons.at(1)->setChecked(true);
                         m_loggingPanel->toggleRecordingFromShortcut();
                     }));

    viewMenu->addSeparator();

    QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);

    const auto addTheme = [&](const QString &title, ThemeManager::Theme theme) {
        QAction *action = themeMenu->addAction(title);
        action->setCheckable(true);
        action->setChecked(m_context.theme && m_context.theme->theme() == theme);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, theme] {
            if (!m_context.theme)
                return;
            m_context.theme->setTheme(theme);
            m_settings.theme = ThemeManager::themeToString(theme);
            m_settings.save(*m_context.settings);
        });
    };
    addTheme(tr("&Dark"), ThemeManager::Theme::Dark);
    addTheme(tr("&Light"), ThemeManager::Theme::Light);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Spotty"), this, [this] {
        QMessageBox::about(this, tr("About Spotty"),
                           tr("<b>Spotty %1</b><br>Modular terminal port monitor.<br><br>"
                              "Configuration: %2")
                               .arg(QLatin1String(SPOTTY_VERSION), Paths::configDir()));
    });
}

void MainWindow::applySettings()
{
    // Единственное место, где настройка превращается в действие. Добавление новой — одна
    // строка здесь и одна в структуре AppSettings.
    QFont font = m_settings.fontFamily.isEmpty()
                     ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                     : QFont(m_settings.fontFamily);
    if (m_settings.fontSize > 0)
        font.setPointSize(m_settings.fontSize);
    m_terminal->setTerminalFont(font);

    m_terminal->setShowTimestamps(m_settings.showTimestamps);
    m_terminal->setRelativeTimestamps(m_settings.relativeTimestamps);
    m_terminal->setTimestampFormat(m_settings.timestampFormat);
    m_terminal->setShowDirection(m_settings.showDirection);
    m_terminal->setHexBytesPerRow(m_settings.hexBytesPerRow);
    m_terminal->setAnsiPalette(m_settings.ansiPalette);
    m_terminal->setViewMode(m_settings.viewMode == QLatin1String("hex")
                                ? TerminalView::ViewMode::Hex
                                : TerminalView::ViewMode::Text);

    // Кнопки панели держим в согласии с настройками, не поднимая при этом их обработчики:
    // иначе применение настроек тут же перезаписало бы их значениями кнопок.
    const QSignalBlocker hexBlocker(m_hexButton);
    const QSignalBlocker timestampBlocker(m_timestampButton);
    const QSignalBlocker directionBlocker(m_directionButton);
    m_hexButton->setChecked(m_settings.viewMode == QLatin1String("hex"));
    m_timestampButton->setChecked(m_settings.showTimestamps);
    m_directionButton->setChecked(m_settings.showDirection);

    if (m_context.session) {
        m_context.session->buffer()->setMaxLines(m_settings.maxLines);
        m_context.session->buffer()->setEncoding(encodingFromName(m_settings.encoding));
        m_context.session->setEchoEnabled(m_settings.localEcho);
        m_context.session->setPacketizerMode(Packetizer::Mode(m_settings.packetizerMode));
        m_context.session->setPacketizerTimeout(m_settings.packetizerTimeoutMs);
        m_context.session->setPacketizerDelimiter(
            QByteArray::fromHex(m_settings.packetizerDelimiterHex.toLatin1()));
        m_context.session->setPacketizerFixedLength(m_settings.packetizerFixedLength);
    }

    m_sendBar->setFormat(DataCodec::Format(m_settings.sendFormat));
    m_sendBar->setTermination(DataCodec::Termination(m_settings.sendTermination));

    m_loggingPanel->applySettings(m_settings);

    applyShortcuts();
}

void MainWindow::applyShortcuts()
{
    for (const ShortcutAction &action : SettingsDialog::shortcutActions()) {
        QAction *target = m_actions.value(action.id);
        if (!target)
            continue;
        target->setShortcut(
            QKeySequence(m_settings.shortcuts.value(action.id, action.defaultSequence)));
    }
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(m_settings, m_terminal->ansiPalette(), m_context.registry,
                         m_context.plugins, this);
    if (InterfaceSettingsPanel *panel = dialog.interfacePanel()) {
        panel->selectInterface(m_context.session ? m_context.session->interfaceId() : QString());
        connect(panel, &InterfaceSettingsPanel::settingsApplied,
                this, &MainWindow::reloadSessionSettingsIfActive);
    }
    connect(&dialog, &SettingsDialog::resetToDefaultsRequested,
            this, &MainWindow::resetToDefaults);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const bool restartNeeded = dialog.requiresRestart();

    m_settings = dialog.settings();
    m_settings.save(*m_context.settings);
    m_context.settings->save();

    if (m_context.theme) {
        m_context.theme->setTheme(
            ThemeManager::themeFromString(m_settings.theme, m_context.theme->theme()));
    }

    applySettings();

    if (restartNeeded) {
        QMessageBox::information(this, tr("Settings"),
                                 tr("The language and single-instance settings take effect "
                                    "after Spotty is restarted."));
    }
}

void MainWindow::resetToDefaults()
{
    // Открытый канал держит настройки, которые вот-вот исчезнут из-под него, и выбирает
    // устройство, чей псевдоним и alias вот-вот пропадут — закрыть и снять выбор нужно
    // раньше, чем реестр забудет, чем он был.
    if (m_context.session) {
        m_context.session->close();
        m_context.session->setInterfaceId(QString());
    }
    m_interfaceBar->setCurrentInterfaceId(QString());

    if (m_context.registry)
        m_context.registry->resetAll();

    if (m_context.history) {
        m_context.history->clear();
        m_context.history->save();
    }

    m_settings = AppSettings{};
    if (m_context.settings) {
        // Вся settings.json, а не только известные ключи AppSettings: геометрия окна,
        // положение сплиттера и прочее состояние UI живут в том же файле и «сброс к
        // умолчаниям» касается их точно так же.
        m_context.settings->clear();
        m_context.settings->save();
    }

    if (m_context.theme) {
        m_context.theme->setTheme(
            ThemeManager::themeFromString(m_settings.theme, m_context.theme->theme()));
    }

    applySettings();
    applyShortcuts();

    statusBar()->showMessage(
        tr("Settings, interfaces and history have been reset to defaults."), 8000);
}

void MainWindow::toggleConnection()
{
    if (!m_context.session)
        return;

    if (m_context.session->isActive())
        m_context.session->close();
    else
        m_context.session->open();
}

void MainWindow::showInterfaceSettings(const QString &interfaceId, const QStringList &invalidFields)
{
    if (!m_context.registry || !m_context.plugins)
        return;

    InterfaceSettingsDialog dialog(m_context.registry, m_context.plugins, interfaceId, this);
    connect(dialog.panel(), &InterfaceSettingsPanel::settingsApplied,
            this, &MainWindow::reloadSessionSettingsIfActive);
    if (!invalidFields.isEmpty())
        dialog.panel()->flagInvalidFields(invalidFields);
    dialog.exec();
}

void MainWindow::reloadSessionSettingsIfActive(const QString &interfaceId)
{
    // Настройки применяются к уже открытому каналу: закрывать его ради смены скорости
    // значило бы дёрнуть DTR и перезагрузить плату. Но только если это тот самый
    // интерфейс — панель настроек умеет переключаться на любое известное устройство, а
    // не только на открытое сейчас.
    if (m_context.session && m_context.session->interfaceId() == interfaceId)
        m_context.session->reloadSettings();
}

void MainWindow::applyChannelState(ChannelState state, const QString &detail)
{
    const bool open = state == ChannelState::Open;

    m_interfaceBar->setChannelState(state, detail);
    m_sendBar->setSendEnabled(open);
    updatePlaceholder(state);

    if (m_macrosPanel)
        m_macrosPanel->setSendEnabled(open);
    if (m_generatorPanel)
        m_generatorPanel->setSendEnabled(open);
    if (m_loggingPanel)
        m_loggingPanel->setInterfaceOpen(open);

    if (open)
        m_sendBar->focusInput();
}

void MainWindow::updatePlaceholder(ChannelState state)
{
    // Просмотр записанного лога — своё, отдельное состояние: интерфейс там ни при чём, а
    // подпись «выберите интерфейс» под открытым файлом сбивала бы с толку.
    if (m_logViewBar && m_logViewBar->isVisible()) {
        m_terminal->setPlaceholderText(tr("This log file is empty"));
        return;
    }

    switch (state) {
    case ChannelState::Open:
        m_terminal->setPlaceholderText(tr("Interface is open — waiting for data"));
        break;
    case ChannelState::Opening:
        m_terminal->setPlaceholderText(tr("Opening the interface…"));
        break;
    case ChannelState::Error:
    case ChannelState::Unavailable:
        // Причина уже сказана в строке состояния и в подсказке кружка; повторять её здесь
        // значило бы сказать одно и то же трижды. Здесь — что делать дальше.
        m_terminal->setPlaceholderText(tr("The interface could not be opened"));
        break;
    case ChannelState::Closed:
        m_terminal->setPlaceholderText(tr("Choose an interface above to see its output here"));
        break;
    }
}

void MainWindow::updateStatistics()
{
    if (!m_context.session) {
        m_statsLabel->clear();
        return;
    }

    const Session::Statistics stats = m_context.session->statistics();

    // Каждое поле дополнено пробелами до фиксированной ширины (см. kStatsValueWidth и
    // соседние константы): без этого RX/TX/скорость меняют ширину на каждое обновление и
    // сдвигают всё, что справа, при каждом тике.
    QString text = tr("RX %1  TX %2")
                       .arg(Formatting::byteCount(stats.bytesReceived)
                                .rightJustified(kStatsValueWidth),
                            Formatting::byteCount(stats.bytesSent)
                                .rightJustified(kStatsValueWidth));

    const QString rate = stats.receiveRateBps > 0.5
                             ? tr("%1/s").arg(Formatting::byteCount(qint64(stats.receiveRateBps)))
                             : QString();
    text += tr("  ·  %1").arg(rate.rightJustified(kStatsRateWidth));

    const QString errors = stats.errorCount > 0
                               ? tr("%n error(s)", nullptr, int(stats.errorCount))
                               : QString();
    text += tr("  ·  %1").arg(errors.leftJustified(kStatsErrorsWidth));

    // Резерв под будущий индикатор строки состояния — держит место заранее, чтобы его
    // появление не сдвинуло уже показанные значения.
    text += QString(kStatsReservedWidth, u' ');

    m_statsLabel->setText(text);
}

void MainWindow::updateControlLines(const QVariantMap &lines)
{
    if (lines.isEmpty()) {
        m_linesLabel->clear();
        return;
    }

    // Порядок фиксирован, а не взят из карты: линии должны стоять на одних и тех же
    // местах, иначе взгляд каждый раз ищет нужную заново.
    static const QStringList order = {QStringLiteral("CTS"), QStringLiteral("DSR"),
                                      QStringLiteral("DCD"), QStringLiteral("RI"),
                                      QStringLiteral("DTR"), QStringLiteral("RTS")};

    QStringList parts;
    for (const QString &name : order) {
        if (!lines.contains(name))
            continue;
        parts << (lines.value(name).toBool() ? name : name.toLower());
    }

    m_linesLabel->setText(parts.join(u' '));
    m_linesLabel->setToolTip(tr("Control lines: uppercase means asserted"));
}

void MainWindow::showLogFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(tr("Cannot open %1: %2")
                                     .arg(QFileInfo(filePath).fileName(), file.errorString()),
                                 8000);
        return;
    }

    if (!m_logBuffer)
        m_logBuffer = new TerminalBuffer(this);
    m_logBuffer->clear();

    // Предел строк тот же, что у живого вывода: огромный лог иначе съел бы всю память,
    // а начало всё равно уехало бы за пределы буфера.
    m_logBuffer->setMaxLines(m_settings.maxLines);
    m_logBuffer->setEncoding(encodingFromName(m_settings.encoding));
    m_logBuffer->append(file.readAll(), DataDirection::Rx, 0, /*terminatesLine=*/true);

    m_terminal->setBuffer(m_logBuffer);
    m_logViewLabel->setText(tr("Viewing log: %1").arg(QFileInfo(filePath).fileName()));
    m_logViewLabel->setToolTip(filePath);
    m_logViewBar->show();
    updatePlaceholder(m_context.session ? m_context.session->state() : ChannelState::Closed);
}

void MainWindow::returnToLiveView()
{
    if (!m_context.session)
        return;

    m_terminal->setBuffer(m_context.session->buffer());
    m_logViewBar->hide();
    updatePlaceholder(m_context.session->state());

    if (m_logBuffer)
        m_logBuffer->clear();
}

void MainWindow::raiseWindow()
{
    // Порядок важен: свёрнутое окно нужно сначала развернуть, иначе activateWindow()
    // ничего не даст.
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::updateIcons()
{
    for (int i = 0; i < m_panelButtons.size() && i < int(std::size(kPanelGlyphs)); ++i)
        m_panelButtons.at(i)->setIcon(MdiIcons::icon(kPanelGlyphs[i], kPanelGlyphSize));
    m_settingsRailButton->setIcon(MdiIcons::icon(mdi::Cog, kPanelGlyphSize));

    m_hexButton->setIcon(MdiIcons::icon(mdi::Hexadecimal, kToolGlyphSize));
    m_timestampButton->setIcon(MdiIcons::icon(mdi::ClockOutline, kToolGlyphSize));
    m_directionButton->setIcon(MdiIcons::icon(mdi::SwapHorizontal, kToolGlyphSize));
    m_clearButton->setIcon(MdiIcons::icon(mdi::Broom, kToolGlyphSize));
    m_followButton->setIcon(MdiIcons::icon(mdi::ArrowCollapseDown, kToolGlyphSize));
}

void MainWindow::restoreWindowState()
{
    SettingsStore *store = m_context.settings;

    const QByteArray geometry = store->value(QLatin1String(kKeyGeometry)).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    const QByteArray splitter = store->value(QLatin1String(kKeySplitter)).toByteArray();
    if (!splitter.isEmpty())
        m_splitter->restoreState(splitter);
    else
        m_splitter->setSizes({320, 860}); // Первый запуск: панель узкая, терминалу — остальное.

    const int panelIndex = store->value(QLatin1String(kKeyPanelIndex), 0).toInt();
    if (panelIndex >= 0 && panelIndex < m_panelButtons.size()) {
        m_panelButtons.at(panelIndex)->setChecked(true);
        m_panelStack->setCurrentIndex(panelIndex);
    }
}

void MainWindow::persistWindowState()
{
    m_context.settings->setValue(QLatin1String(kKeyGeometry), saveGeometry());
    m_context.settings->setValue(QLatin1String(kKeySplitter), m_splitter->saveState());
    m_context.settings->setValue(QLatin1String(kKeyPanelIndex), m_panelStack->currentIndex());
    // Явный save(), а не отложенный: приложение вот-вот завершится, ждать таймер некому.
    m_context.settings->save();

    if (m_context.history)
        m_context.history->save();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    persistWindowState();

    // Канал закрывается до разрушения окна: поток ввода-вывода должен остановиться, пока
    // объекты, на которые он ссылается, ещё живы.
    if (m_context.session)
        m_context.session->close();

    QMainWindow::closeEvent(event);
}

} // namespace spotty
