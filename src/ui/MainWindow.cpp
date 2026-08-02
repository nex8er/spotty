/**
 * \file MainWindow.cpp
 * \brief Реализация spotty::MainWindow.
 */
#include "MainWindow.h"

#include "Formatting.h"
#include "InterfaceBar.h"
#include "SendBar.h"
#include "dialogs/InterfaceSettingsDialog.h"
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
#include <spotty/api/IInterfacePlugin.h>

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QFile>
#include <QFileInfo>
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

// Ключи настроек. Собраны здесь, чтобы опечатка в строке не разошлась между записью и
// чтением: такую ошибку компилятор не поймает, а проявится она лишь как «настройка не
// сохраняется».
constexpr auto kKeyGeometry = "window/geometry";
constexpr auto kKeySplitter = "window/splitter";
constexpr auto kKeyPanelIndex = "window/panelIndex";
constexpr auto kKeyTheme = "appearance/theme";
constexpr auto kKeyInterface = "session/interfaceId";
constexpr auto kKeyViewMode = "terminal/viewMode";
constexpr auto kKeyTimestamps = "terminal/timestamps";
constexpr auto kKeyDirection = "terminal/showDirection";
constexpr auto kKeyMaxLines = "terminal/maxLines";
constexpr auto kKeySendFormat = "send/format";
constexpr auto kKeySendTermination = "send/termination";
constexpr auto kKeyAutoOpen = "session/autoOpen";

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

} // namespace

MainWindow::MainWindow(const AppContext &context, QWidget *parent)
    : QMainWindow(parent)
    , m_context(context)
{
    setWindowTitle(QStringLiteral("Spotty %1").arg(QLatin1String(SPOTTY_VERSION)));

    buildUi();
    buildMenus();
    restoreState();

    if (m_context.theme)
        connect(m_context.theme, &ThemeManager::themeChanged, this, [this] { updateIcons(); });
    updateIcons();
}

void MainWindow::buildUi()
{
    m_interfaceBar = new InterfaceBar(m_context, this);
    m_terminal = new TerminalView(this);
    m_terminal->setBuffer(m_context.session ? m_context.session->buffer() : nullptr);
    m_terminal->setThemeManager(m_context.theme);

    m_sendBar = new SendBar(m_context.history, this);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_terminal);
    m_splitter->addWidget(buildSidePanel());
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    // Панель должна полностью схлопываться: при работе с длинными строками терминалу
    // нужна вся ширина окна.
    m_splitter->setChildrenCollapsible(true);

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

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_interfaceBar);
    layout->addWidget(buildTerminalToolbar());
    layout->addWidget(m_logViewBar);
    layout->addWidget(m_splitter, 1);
    layout->addWidget(m_sendBar);
    setCentralWidget(central);

    m_statsLabel = new QLabel(this);
    m_linesLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_linesLabel);
    statusBar()->addPermanentWidget(m_statsLabel);

    // --- Связывание -----------------------------------------------------------------

    connect(m_interfaceBar, &InterfaceBar::interfaceSelected, this, [this](const QString &id) {
        m_context.settings->setValue(QLatin1String(kKeyInterface), id);
        if (m_context.session)
            m_context.session->setInterfaceId(id);
    });

    connect(m_interfaceBar, &InterfaceBar::toggleOpenRequested,
            this, &MainWindow::toggleConnection);

    connect(m_interfaceBar, &InterfaceBar::settingsRequested,
            this, &MainWindow::showInterfaceSettings);

    connect(m_sendBar, &SendBar::sendRequested, this, [this](const QByteArray &data) {
        if (m_context.session)
            m_context.session->send(data);
    });

    connect(m_sendBar, &SendBar::optionsChanged, this, [this] {
        m_context.settings->setValue(QLatin1String(kKeySendFormat), int(m_sendBar->format()));
        m_context.settings->setValue(QLatin1String(kKeySendTermination),
                                     int(m_sendBar->termination()));
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
    m_logBuffer->setMaxLines(m_context.settings->value(QLatin1String(kKeyMaxLines), 20000).toInt());
    m_logBuffer->append(file.readAll(), DataDirection::Rx, 0, /*terminatesLine=*/true);

    m_terminal->setBuffer(m_logBuffer);
    m_logViewLabel->setText(tr("Viewing log: %1").arg(QFileInfo(filePath).fileName()));
    m_logViewLabel->setToolTip(filePath);
    m_logViewBar->show();
}

void MainWindow::returnToLiveView()
{
    if (!m_context.session)
        return;

    m_terminal->setBuffer(m_context.session->buffer());
    m_logViewBar->hide();

    if (m_logBuffer)
        m_logBuffer->clear();
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
        m_context.settings->setValue(QLatin1String(kKeyViewMode),
                                     hex ? QStringLiteral("hex") : QStringLiteral("text"));
    });
    connect(m_timestampButton, &QToolButton::toggled, this, [this](bool show) {
        m_terminal->setShowTimestamps(show);
        m_context.settings->setValue(QLatin1String(kKeyTimestamps), show);
    });
    connect(m_directionButton, &QToolButton::toggled, this, [this](bool show) {
        m_terminal->setShowDirection(show);
        m_context.settings->setValue(QLatin1String(kKeyDirection), show);
    });
    connect(m_clearButton, &QToolButton::clicked, this, [this] {
        if (m_context.session)
            m_context.session->buffer()->clear();
    });
    connect(m_followButton, &QToolButton::toggled, this, [this](bool follow) {
        m_terminal->setFollowTail(follow);
    });

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(2);
    layout->addWidget(m_hexButton);
    layout->addWidget(m_timestampButton);
    layout->addWidget(m_directionButton);
    layout->addSpacing(8);
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
    fileMenu->addAction(tr("&Settings..."))->setEnabled(false);
    fileMenu->addSeparator();
    QAction *quit = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu *interfaceMenu = menuBar()->addMenu(tr("&Interface"));
    QAction *toggle = interfaceMenu->addAction(tr("&Open / Close"), this,
                                               &MainWindow::toggleConnection);
    toggle->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
    interfaceMenu->addSeparator();

    // Ручное управление линиями: у многих плат DTR и RTS заведены на сброс и загрузчик,
    // и дёрнуть их вручную — обычная отладочная операция.
    interfaceMenu->addAction(tr("Toggle &DTR"), this, [this] {
        if (m_context.session) {
            const bool current = m_context.session->controlLines()
                                     .value(QStringLiteral("DTR")).toBool();
            m_context.session->setControlLine(QStringLiteral("DTR"), !current);
        }
    });
    interfaceMenu->addAction(tr("Toggle &RTS"), this, [this] {
        if (m_context.session) {
            const bool current = m_context.session->controlLines()
                                     .value(QStringLiteral("RTS")).toBool();
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
    hexAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    connect(hexAction, &QAction::toggled, m_hexButton, &QToolButton::setChecked);
    connect(m_hexButton, &QToolButton::toggled, hexAction, &QAction::setChecked);

    QAction *timestampAction = viewMenu->addAction(tr("&Timestamps"));
    timestampAction->setCheckable(true);
    connect(timestampAction, &QAction::toggled, m_timestampButton, &QToolButton::setChecked);
    connect(m_timestampButton, &QToolButton::toggled, timestampAction, &QAction::setChecked);

    QAction *relativeAction = viewMenu->addAction(tr("Timestamps &relative to previous line"));
    relativeAction->setCheckable(true);
    connect(relativeAction, &QAction::toggled, this,
            [this](bool on) { m_terminal->setRelativeTimestamps(on); });

    viewMenu->addSeparator();

    QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);

    const auto addTheme = [&](const QString &title, ThemeManager::Theme theme) {
        QAction *action = themeMenu->addAction(title);
        action->setCheckable(true);
        action->setChecked(m_context.theme && m_context.theme->theme() == theme);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, theme] { setTheme(theme); });
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

void MainWindow::toggleConnection()
{
    if (!m_context.session)
        return;

    if (m_context.session->isActive())
        m_context.session->close();
    else
        m_context.session->open();
}

void MainWindow::showInterfaceSettings(const QString &interfaceId)
{
    if (interfaceId.isEmpty() || !m_context.registry || !m_context.plugins)
        return;

    const InterfaceEntry *entry = m_context.registry->entry(interfaceId);
    if (!entry)
        return;

    IInterfacePlugin *plugin = m_context.plugins->plugin(entry->descriptor.pluginId);
    if (!plugin)
        return;

    InterfaceSettingsDialog dialog(entry->descriptor.systemName, plugin->settingsSchema(),
                                   m_context.registry->settingsFor(interfaceId),
                                   entry->alias, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_context.registry->setAlias(interfaceId, dialog.alias());
    m_context.registry->setSettingsFor(interfaceId, dialog.values());

    // Настройки применяются к уже открытому каналу: закрывать его ради смены скорости
    // значило бы дёрнуть DTR и перезагрузить плату.
    if (m_context.session)
        m_context.session->reloadSettings();
}

void MainWindow::applyChannelState(ChannelState state, const QString &detail)
{
    const bool open = state == ChannelState::Open;

    m_interfaceBar->setChannelState(state, detail);
    m_sendBar->setSendEnabled(open);

    if (m_macrosPanel)
        m_macrosPanel->setSendEnabled(open);
    if (m_generatorPanel)
        m_generatorPanel->setSendEnabled(open);
    if (m_loggingPanel)
        m_loggingPanel->setInterfaceOpen(open);

    if (open)
        m_sendBar->focusInput();
}

void MainWindow::updateStatistics()
{
    if (!m_context.session) {
        m_statsLabel->clear();
        return;
    }

    const Session::Statistics stats = m_context.session->statistics();
    QString text = tr("RX %1  TX %2")
                       .arg(Formatting::byteCount(stats.bytesReceived),
                            Formatting::byteCount(stats.bytesSent));

    if (stats.receiveRateBps > 0.5)
        text += tr("  ·  %1/s").arg(Formatting::byteCount(qint64(stats.receiveRateBps)));
    if (stats.errorCount > 0)
        text += tr("  ·  %n error(s)", nullptr, int(stats.errorCount));

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

void MainWindow::setTheme(ThemeManager::Theme theme)
{
    if (!m_context.theme)
        return;

    m_context.theme->setTheme(theme);
    m_context.settings->setValue(QLatin1String(kKeyTheme), ThemeManager::themeToString(theme));
}

void MainWindow::updateIcons()
{
    for (int i = 0; i < m_panelButtons.size() && i < int(std::size(kPanelGlyphs)); ++i)
        m_panelButtons.at(i)->setIcon(MdiIcons::icon(kPanelGlyphs[i], kPanelGlyphSize));

    m_hexButton->setIcon(MdiIcons::icon(mdi::Hexadecimal, kToolGlyphSize));
    m_timestampButton->setIcon(MdiIcons::icon(mdi::ClockOutline, kToolGlyphSize));
    m_directionButton->setIcon(MdiIcons::icon(mdi::SwapHorizontal, kToolGlyphSize));
    m_clearButton->setIcon(MdiIcons::icon(mdi::Broom, kToolGlyphSize));
    m_followButton->setIcon(MdiIcons::icon(mdi::ArrowCollapseDown, kToolGlyphSize));
}

void MainWindow::restoreState()
{
    SettingsStore *settings = m_context.settings;

    const QByteArray geometry = settings->value(QLatin1String(kKeyGeometry)).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    const QByteArray splitter = settings->value(QLatin1String(kKeySplitter)).toByteArray();
    if (!splitter.isEmpty())
        m_splitter->restoreState(splitter);

    const int panelIndex = settings->value(QLatin1String(kKeyPanelIndex), 0).toInt();
    if (panelIndex >= 0 && panelIndex < m_panelButtons.size()) {
        m_panelButtons.at(panelIndex)->setChecked(true);
        m_panelStack->setCurrentIndex(panelIndex);
    }

    m_hexButton->setChecked(settings->value(QLatin1String(kKeyViewMode)).toString()
                            == QLatin1String("hex"));
    m_timestampButton->setChecked(settings->value(QLatin1String(kKeyTimestamps), false).toBool());
    m_directionButton->setChecked(settings->value(QLatin1String(kKeyDirection), true).toBool());

    m_terminal->setBuffer(m_context.session ? m_context.session->buffer() : nullptr);
    if (m_context.session) {
        m_context.session->buffer()->setMaxLines(
            settings->value(QLatin1String(kKeyMaxLines), 20000).toInt());
    }

    m_sendBar->setFormat(DataCodec::Format(
        settings->value(QLatin1String(kKeySendFormat), int(DataCodec::Format::Text)).toInt()));
    m_sendBar->setTermination(DataCodec::Termination(
        settings->value(QLatin1String(kKeySendTermination),
                        int(DataCodec::Termination::CrLf)).toInt()));

    // Интерфейс восстанавливается последним: обработчик выбора обращается к сессии,
    // которая к этому моменту уже должна быть настроена.
    const QString interfaceId = settings->value(QLatin1String(kKeyInterface)).toString();
    m_interfaceBar->setCurrentInterfaceId(interfaceId);
    if (m_context.session && !interfaceId.isEmpty()) {
        m_context.session->setInterfaceId(interfaceId);

        // По умолчанию выключено. Открытие порта — действие с последствиями: оно дёргает
        // DTR, а у многих плат это сброс, и оно же перехватывает порт у другой программы,
        // которой он мог быть нужен. Запускать это молча при каждом старте нельзя;
        // пользователь включает настройку сам, если хочет.
        if (settings->value(QLatin1String(kKeyAutoOpen), false).toBool())
            m_context.session->open();
    }
}

void MainWindow::persistState()
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
    persistState();

    // Канал закрывается до разрушения окна: поток ввода-вывода должен остановиться, пока
    // объекты, на которые он ссылается, ещё живы.
    if (m_context.session)
        m_context.session->close();

    QMainWindow::closeEvent(event);
}

} // namespace spotty
