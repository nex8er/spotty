/**
 * \file MainWindow.cpp
 * \brief Реализация spotty::MainWindow.
 */
#include "MainWindow.h"

#include <spotty/data/Formatting.h>
#include "InterfaceBar.h"
#include "SendBar.h"
#include "dialogs/InterfaceSettingsDialog.h"
#include "dialogs/InterfaceSettingsPanel.h"
#include "dialogs/SettingsDialog.h"
#include "OverlayLayer.h"
#include "PanelHostImpl.h"
#include "PanelPluginRegistry.h"
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
// Панель запоминается идентификатором, а не номером страницы: при плагинах набор
// перестал быть постоянным, и сохранённый номер после установки или снятия плагина
// указывал бы на чужую панель.
constexpr auto kKeyPanelId = "window/panelId";
constexpr auto kKeyPanelIndexLegacy = "window/panelIndex";
constexpr auto kKeyTerminalSplitter = "window/terminalSplitter";
constexpr auto kKeyInterface = "session/interfaceId";

/// \brief Прежний порядок панелей — для переноса kKeyPanelIndexLegacy в kKeyPanelId.
const char *const kLegacyPanelIds[] = {"macros", "logging", "search", "generator"};

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

    // Разделитель создаётся всегда, даже когда ни один плагин не просит своей полосы:
    // с единственным ребёнком он неотличим от голого виджета, а условная ветка «когда
    // полос нет — по-старому» была бы вторым путём, который сломается первым.
    m_terminalSplitter = new QSplitter(Qt::Vertical, this);
    m_terminalSplitter->setHandleWidth(metrics.splitterHandle);
    m_terminalSplitter->setChildrenCollapsible(false);
    m_terminalCard = makeCard(terminalBlock);
    m_terminalSplitter->addWidget(m_terminalCard);
    m_terminalSplitter->setStretchFactor(0, 1);

    auto *rightColumn = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(metrics.gap, metrics.gap, metrics.gap, metrics.gap);
    rightLayout->setSpacing(metrics.gap);
    rightLayout->addWidget(makeCard(m_interfaceBar));
    rightLayout->addWidget(m_terminalSplitter, 1);
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

    // --- Панели плагинов ----------------------------------------------------------------
    //
    // Строятся последними: хост панели подписывается в конструкторе на сигналы терминала
    // и сессии, и до этого места их ещё нет. Связывать панели поимённо больше не нужно —
    // всё, что им доступно, они берут через свой хост.
    m_overlay = new OverlayLayer(m_terminal);
    buildPanelPlugins();
    installPluginFilters();

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
    m_railLayout = new QVBoxLayout(rail);
    m_railLayout->setContentsMargins(4, 6, 4, 6);
    m_railLayout->setSpacing(4);

    m_panelStack = new QStackedWidget(panel);

    // Страницы добавит buildPanelPlugins() — к этому моменту ни терминала, ни строки
    // отправки ещё нет, а хост панели подписывается на них в конструкторе.
    m_railLayout->addStretch(1);

    // Настройки — не панель, а действие, поэтому вне группы переключателей: растяжка
    // отделяет её от них и прижимает к низу рейки.
    m_settingsRailButton = new QToolButton(rail);
    m_settingsRailButton->setAutoRaise(true);
    m_settingsRailButton->setToolTip(tr("Settings"));
    m_settingsRailButton->setIconSize(QSize(kPanelGlyphSize, kPanelGlyphSize));
    m_railLayout->addWidget(m_settingsRailButton);
    connect(m_settingsRailButton, &QToolButton::clicked, this, &MainWindow::showSettingsDialog);

    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(rail);
    layout->addWidget(m_panelStack, 1);

    return panel;
}

void MainWindow::buildPanelPlugins()
{
    PanelPluginRegistry *registry = m_context.panels;
    if (!registry)
        return;

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);

    for (const PanelPluginRegistry::PanelEntry &entry : registry->panels()) {
        const QString pluginId = entry.plugin->pluginId();

        // Хост один на плагин, а не на панель: настройки, каталог данных и сочетания
        // клавиш принадлежат плагину целиком.
        PanelHostImpl *host = m_hosts.value(pluginId);
        if (!host) {
            host = new PanelHostImpl(m_context, this, pluginId);
            m_hosts.insert(pluginId, host);
        }

        QWidget *widget = entry.plugin->createPanel(entry.descriptor.id, host, nullptr);
        if (!widget) {
            qWarning("Panel plugin \"%s\" declared panel \"%s\" but refused to create it",
                     qUtf8Printable(pluginId), qUtf8Printable(entry.descriptor.id));
            continue;
        }

        switch (entry.descriptor.placement) {
        case PanelPlacement::Rail:
            addRailPanel(entry.descriptor, widget, group);
            break;
        case PanelPlacement::Splitter:
            addSplitterPanel(entry.descriptor, widget);
            break;
        case PanelPlacement::Overlay:
            m_overlay->addOverlay(widget, entry.descriptor.anchor,
                                  entry.descriptor.mouseTransparent);
            break;
        }
    }

    if (!m_railPanels.isEmpty()) {
        m_railPanels.first().button->setChecked(true);
        m_panelStack->setCurrentIndex(0);
    }
}

void MainWindow::addRailPanel(const PanelDescriptor &descriptor, QWidget *widget,
                              QButtonGroup *group)
{
    const int index = m_panelStack->count();
    m_panelStack->addWidget(widget);

    auto *button = new QToolButton(m_panelStack->parentWidget());
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setToolTip(descriptor.title);
    button->setIconSize(QSize(kPanelGlyphSize, kPanelGlyphSize));
    // Панель без значка подписывается первой буквой: пустая кнопка в рейке неотличима от
    // промежутка, и нажать на неё можно только случайно.
    if (descriptor.glyph == 0)
        button->setText(descriptor.title.left(1).toUpper());

    group->addButton(button, index);
    // Перед растяжкой и кнопкой настроек, которые уже стоят в конце рейки.
    m_railLayout->insertWidget(index, button);

    connect(button, &QToolButton::clicked, this,
            [this, index] { m_panelStack->setCurrentIndex(index); });

    m_railPanels.append({descriptor.id, widget, button});
}

void MainWindow::addSplitterPanel(const PanelDescriptor &descriptor, QWidget *widget)
{
    // Порядок внутри разделителя: всё, что «сверху», идёт до терминала, остальное после.
    // Индекс терминала ищется каждый раз заново — панелей может быть несколько, и он
    // сдвигается по мере их добавления.
    const int terminalIndex = m_terminalSplitter->indexOf(m_terminalCard);
    const int at = descriptor.side == PanelSide::Above ? terminalIndex : terminalIndex + 1;

    m_terminalSplitter->insertWidget(at, makeCard(widget));
    m_terminalSplitter->setStretchFactor(at, 0);
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

    m_actions.insert(QStringLiteral("send.focus"),
                     viewMenu->addAction(tr("Focus &send bar"), this,
                                         [this] { m_sendBar->focusInput(); }));

    // «Найти» и «Начать запись» отсюда ушли: они принадлежат панелям поиска и журнала, а
    // те объявляют свои сочетания сами через IPanelHost::setShortcuts(). Прежде окно
    // знало о них и переключало страницу по жёсткому номеру — с плагинами такой номер
    // означал бы разное при разном наборе установленных панелей.

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

    applyShortcuts();
}

void MainWindow::applyShortcuts()
{
    for (const ShortcutAction &action : SettingsDialog::builtinShortcutActions()) {
        QAction *target = m_actions.value(action.id);
        if (!target)
            continue;
        target->setShortcut(
            QKeySequence(m_settings.shortcuts.value(action.id, action.defaultSequence)));
    }

    // Сочетания панелей переопределяются через их хосты: сами действия создал хост, и
    // только он знает, какому идентификатору какое соответствует.
    for (auto it = m_hosts.cbegin(); it != m_hosts.cend(); ++it) {
        for (const PanelShortcut &shortcut : it.value()->configurableShortcuts()) {
            const QString fullId = QStringLiteral("%1.%2").arg(it.key(), shortcut.id);
            const QString stored = m_settings.shortcuts.value(fullId);
            if (!stored.isEmpty())
                it.value()->applyShortcutOverride(fullId, QKeySequence(stored));
        }
    }
}

QList<ShortcutAction> MainWindow::allShortcutActions() const
{
    QList<ShortcutAction> actions = SettingsDialog::builtinShortcutActions();

    for (auto it = m_hosts.cbegin(); it != m_hosts.cend(); ++it) {
        for (const PanelShortcut &shortcut : it.value()->configurableShortcuts()) {
            actions.append({QStringLiteral("%1.%2").arg(it.key(), shortcut.id),
                            shortcut.title,
                            shortcut.defaultSequence.toString(QKeySequence::PortableText)});
        }
    }
    return actions;
}

QHash<QString, QVariantMap> MainWindow::currentPluginSettings() const
{
    QHash<QString, QVariantMap> values;
    if (!m_context.panels || !m_context.settings)
        return values;

    for (IPanelPlugin *plugin : m_context.panels->plugins()) {
        if (plugin->settingsSchema().isEmpty())
            continue;
        values.insert(plugin->pluginId(),
                      m_context.settings->group(QStringLiteral("plugins/%1")
                                                    .arg(plugin->pluginId())));
    }
    return values;
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(m_settings, m_terminal->ansiPalette(), m_context.registry,
                          m_context.plugins, m_context.panels, currentPluginSettings(),
                          allShortcutActions(), this);
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

    // Настройки плагинов пишутся поддеревьями: ключи внутри схемы знает только плагин, и
    // перечислять их здесь значило бы завести второе место, где они объявлены.
    const QHash<QString, QVariantMap> pluginValues = dialog.pluginSettings();
    for (auto it = pluginValues.cbegin(); it != pluginValues.cend(); ++it) {
        m_context.settings->setGroup(QStringLiteral("plugins/%1").arg(it.key()), it.value());
        if (PanelHostImpl *host = m_hosts.value(it.key()))
            host->notifySettingsReset();
    }

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

    // Прежде панели после сброса оставались с состоянием, прочитанным при запуске: файл
    // настроек уже чист, а панель показывает старое. Теперь каждая перечитывает своё.
    for (PanelHostImpl *host : std::as_const(m_hosts))
        host->notifySettingsReset();

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

    // Панели узнают о состоянии через свои хосты: окну незачем знать, какая из них что
    // из него извлечёт.
    for (PanelHostImpl *host : std::as_const(m_hosts))
        host->notifyChannelState(state);

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

bool MainWindow::showLogFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(tr("Cannot open %1: %2")
                                     .arg(QFileInfo(filePath).fileName(), file.errorString()),
                                 8000);
        return false;
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

    // Слои поверх вывода относятся к живому потоку: график, нарисованный по нему, поверх
    // чужого файла означал бы не то, что показывает.
    if (m_overlay)
        m_overlay->hide();
    return true;
}

void MainWindow::returnToLiveView()
{
    if (!m_context.session)
        return;

    m_terminal->setBuffer(m_context.session->buffer());
    m_logViewBar->hide();
    updatePlaceholder(m_context.session->state());

    if (m_overlay)
        m_overlay->show();

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
    if (m_context.panels) {
        for (const PanelPluginRegistry::PanelEntry &entry : m_context.panels->panels()) {
            const RailPanel *slot = nullptr;
            for (const RailPanel &candidate : std::as_const(m_railPanels)) {
                if (candidate.id == entry.descriptor.id) {
                    slot = &candidate;
                    break;
                }
            }
            if (slot && entry.descriptor.glyph != 0)
                slot->button->setIcon(MdiIcons::icon(entry.descriptor.glyph, kPanelGlyphSize));
        }
    }
    m_settingsRailButton->setIcon(MdiIcons::icon(mdi::Cog, kPanelGlyphSize));

    // Значки внутри панелей перекрашивают они сами, получив themeChanged() от хоста.
    for (PanelHostImpl *host : std::as_const(m_hosts))
        host->notifyThemeChanged();

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

    const QByteArray terminalSplitter =
        store->value(QLatin1String(kKeyTerminalSplitter)).toByteArray();
    // Восстанавливаем только при совпавшем числе секций: иначе набор плагинов сменился, и
    // сохранённые размеры относятся к другой раскладке.
    if (!terminalSplitter.isEmpty()) {
        const QList<int> saved = m_terminalSplitter->sizes();
        m_terminalSplitter->restoreState(terminalSplitter);
        if (m_terminalSplitter->sizes().size() != saved.size())
            m_terminalSplitter->setSizes(saved);
    }

    QString panelId = store->value(QLatin1String(kKeyPanelId)).toString();
    if (panelId.isEmpty()) {
        // Перенос со старого числового ключа. Читается один раз и тут же удаляется:
        // держать оба означало бы держать два источника одной правды.
        const QVariant legacy = store->value(QLatin1String(kKeyPanelIndexLegacy));
        if (legacy.isValid()) {
            const int index = legacy.toInt();
            if (index >= 0 && index < int(std::size(kLegacyPanelIds)))
                panelId = QLatin1String(kLegacyPanelIds[index]);
            store->remove(QLatin1String(kKeyPanelIndexLegacy));
        }
    }

    // Панель могла исчезнуть вместе со снятым плагином — тогда остаётся первая по порядку,
    // уже выбранная в buildPanelPlugins().
    if (!panelId.isEmpty())
        activatePanel(panelId);
}

void MainWindow::activatePanel(const QString &panelId)
{
    for (int i = 0; i < m_railPanels.size(); ++i) {
        if (m_railPanels.at(i).id != panelId)
            continue;
        m_railPanels.at(i).button->setChecked(true);
        m_panelStack->setCurrentIndex(i);
        return;
    }
}

void MainWindow::composeInSendBar(const QString &text, DataCodec::Format format)
{
    m_sendBar->setFormat(format);
    m_sendBar->setText(text);
    m_sendBar->focusInput();
}

void MainWindow::showStatusMessage(const QString &message)
{
    statusBar()->showMessage(message, 8000);
}

bool MainWindow::showDocument(const QString &filePath, const QString &title)
{
    Q_UNUSED(title);
    return showLogFile(filePath);
}

void MainWindow::installPluginFilters()
{
    if (!m_context.session || !m_context.panels)
        return;

    for (IPanelPlugin *plugin : m_context.panels->plugins()) {
        if (IDataFilter *filter = plugin->dataFilter()) {
            m_context.session->addDataFilter(filter, plugin->filterOrder(),
                                             plugin->pluginId());
        }
    }
}

void MainWindow::persistWindowState()
{
    m_context.settings->setValue(QLatin1String(kKeyGeometry), saveGeometry());
    m_context.settings->setValue(QLatin1String(kKeySplitter), m_splitter->saveState());
    m_context.settings->setValue(QLatin1String(kKeyTerminalSplitter),
                                 m_terminalSplitter->saveState());
    const int current = m_panelStack->currentIndex();
    if (current >= 0 && current < m_railPanels.size())
        m_context.settings->setValue(QLatin1String(kKeyPanelId), m_railPanels.at(current).id);
    // Явный save(), а не отложенный: приложение вот-вот завершится, ждать таймер некому.
    m_context.settings->save();

    if (m_context.history)
        m_context.history->save();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Панели дописывают файлы и останавливают запись до того, как их разрушат вместе с
    // окном: журнал иначе остался бы с недописанным хвостом.
    for (PanelHostImpl *host : std::as_const(m_hosts))
        host->notifyAboutToClose();

    // Звенья цепочки принадлежат плагинам и переживут сессию — снимаем их явно, пока
    // панели, на состояние которых они ссылаются, ещё целы.
    if (m_context.session && m_context.panels) {
        for (IPanelPlugin *plugin : m_context.panels->plugins()) {
            if (IDataFilter *filter = plugin->dataFilter())
                m_context.session->removeDataFilter(filter);
        }
    }

    persistWindowState();

    // Канал закрывается до разрушения окна: поток ввода-вывода должен остановиться, пока
    // объекты, на которые он ссылается, ещё живы.
    if (m_context.session)
        m_context.session->close();

    QMainWindow::closeEvent(event);
}

} // namespace spotty
