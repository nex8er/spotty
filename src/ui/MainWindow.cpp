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
#include "theme/EmbeddedFonts.h"
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
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr int kPanelGlyphSize = 20;
constexpr int kStatusGlyphSize = 14;

/**
 * \brief Наименьшая ширина содержимого боковой панели, px.
 *
 * Задаётся стопке страниц, а не панели целиком: рейка значков остаётся видимой и в
 * свёрнутом виде, и предел, заданный панели, не дал бы ей сузиться до одной рейки.
 * Значение подобрано по самой тесной панели — таблице правил подсветки с тремя колонками.
 */
constexpr int kPanelStackMinWidth = 260;

/// \brief Ширина боковой панели при первом запуске, px.
constexpr int kDefaultSidePanelWidth = 320;
constexpr int kToolGlyphSize = 18;

/**
 * \brief Ширины полей строки состояния в знаках моноширинного шрифта.
 *
 * Значение дополняется пробелами до этой ширины, поэтому смена «340 Б» на «1.2 КиБ» не
 * сдвигает то, что стоит правее. Счётчик ошибок в выравнивании не нуждается: он крайний
 * слева среди постоянных полей и появляется целиком.
 */
constexpr int kStatsValueWidth = 10;   // "9999.9 GiB"
constexpr int kStatsRateWidth = 12;    // "9999.9 GiB/s"

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
constexpr auto kKeySidePanel = "window/sidePanelVisible";
// Ширина развёрнутой панели хранится отдельно от состояния разделителя: в свёрнутом виде
// разделитель знает лишь ширину рейки, и выбранная пользователем ширина терялась бы при
// каждом закрытии панели.
constexpr auto kKeySidePanelWidth = "window/sidePanelWidth";
constexpr auto kKeyViewMode = "window/viewMode";
constexpr auto kKeyViewStrip = "window/viewStrip";
constexpr auto kKeyInterface = "session/interfaceId";

/// \brief Прежний порядок панелей — для переноса kKeyPanelIndexLegacy в kKeyPanelId.
const char *const kLegacyPanelIds[] = {"macros", "logging", "search", "generator"};

/// \brief Кодировка приёма по имени из настроек.
QStringConverter::Encoding encodingFromName(const QString &name)
{
    return name == QLatin1String("latin1") ? QStringConverter::Latin1
                                           : QStringConverter::Utf8;
}

/// \brief Способ показа нечитаемых символов по имени из настроек.
TerminalView::UnreadableMode unreadableModeFromName(const QString &name)
{
    if (name == QLatin1String("hide"))
        return TerminalView::UnreadableMode::Hide;
    if (name == QLatin1String("line"))
        return TerminalView::UnreadableMode::HideLine;
    return TerminalView::UnreadableMode::Dots;
}

/// \brief Имя способа показа нечитаемых символов для настроек.
QString unreadableModeName(TerminalView::UnreadableMode mode)
{
    switch (mode) {
    case TerminalView::UnreadableMode::Hide:
        return QStringLiteral("hide");
    case TerminalView::UnreadableMode::HideLine:
        return QStringLiteral("line");
    case TerminalView::UnreadableMode::Dots:
        break;
    }
    return QStringLiteral("dots");
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

    // Вторая полоса выбора и вторая сессия создаются сразу, но скрыты: держать их
    // наготове дешевле, чем создавать при переключении режима и заново связывать все
    // сигналы. Пустая сессия не открывает каналов и ничего не стоит.
    // Прячется только карточка. Скрыть здесь сам виджет значило бы оставить его скрытым
    // навсегда: показ карточки не показывает явно спрятанного ребёнка.
    m_secondBar = new InterfaceBar(m_context, this);

    if (m_context.plugins && m_context.registry) {
        m_secondSession = new Session(m_context.plugins, m_context.registry, this);
    }

    // Общий буфер принадлежит окну: он переживает переключение режима, и обе сессии
    // ссылаются на него, помечая свои строки номером источника.
    m_sharedBuffer = new TerminalBuffer(this);
    m_terminal = new TerminalView(this);
    m_terminal->setObjectName(QStringLiteral("terminalView"));
    m_terminal->setBuffer(m_context.session ? m_context.session->buffer() : nullptr);
    m_terminal->setThemeManager(m_context.theme);
    if (m_context.registry) {
        connect(m_terminal, &TerminalView::scrollInteraction, m_context.registry,
                [registry = m_context.registry] { registry->deferPolling(); });
    }

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
    m_terminalSplitter->addWidget(m_terminal);
    m_terminalSplitter->setStretchFactor(0, 1);

    // --- Область вывода: панель управления и то, что она показывает ---------------------
    //
    // Панель лежит **над** разделителем, а не внутри карточки терминала. Прежде она была
    // частью терминала, и в режиме графика исчезала вместе с ним — вместе с
    // переключателем режима, то есть выйти из графика было нечем.
    //
    // Карточка одна на всю область: и терминал, и полосы плагинов живут внутри её рамки,
    // своих у них нет. Вложенные рамки читались бы как две границы подряд.
    auto *outputBlock = new QWidget(this);
    auto *outputLayout = new QVBoxLayout(outputBlock);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(0);
    outputLayout->addWidget(buildTerminalToolbar());
    outputLayout->addWidget(m_logViewBar);
    outputLayout->addWidget(m_terminalSplitter, 1);

    auto *rightColumn = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightColumn);
    // Слева поля нет: там стоит разделитель, и его ширина уже играет роль зазора. Поле
    // прибавлялось бы к ней, и просвет у боковой панели выходил вдвое шире остальных.
    rightLayout->setContentsMargins(0, metrics.gap, metrics.gap, metrics.gap);
    rightLayout->setSpacing(metrics.gap);
    rightLayout->addWidget(makeCard(m_interfaceBar));
    m_secondBarCard = makeCard(m_secondBar);
    m_secondBarCard->hide();
    rightLayout->addWidget(m_secondBarCard);
    rightLayout->addWidget(makeCard(outputBlock), 1);
    rightLayout->addWidget(makeCard(m_sendBar));

    // --- Панель слева во всю высоту ---------------------------------------------------
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_sidePanel = buildSidePanel();
    m_splitter->addWidget(m_sidePanel);
    m_splitter->addWidget(rightColumn);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    // Схлопывать панель мышью незачем: за это отвечают кнопки рейки, и они оставляют её
    // саму на месте. Схлопнутая же разделителем панель исчезает целиком, вместе с
    // единственным способом вернуть её обратно, — состояние, из которого нет выхода.
    m_splitter->setChildrenCollapsible(false);

    // Ширину, выбранную мышью, запоминаем сразу: сохранить её при выходе нельзя — к тому
    // моменту панель может быть свёрнута, и разделитель будет знать лишь ширину рейки.
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        if (!m_sidePanelExpanded)
            return;
        const QList<int> sizes = m_splitter->sizes();
        if (sizes.isEmpty())
            return;
        m_sidePanelWidth = sizes.first();
        m_context.settings->setValue(QLatin1String(kKeySidePanelWidth), m_sidePanelWidth);
    });
    // Захват шире рисуемой линии. Прежний однопиксельный разделитель поймать курсором
    // почти невозможно: промах не делает ничего, и разделитель выглядит неработающим.
    m_splitter->setHandleWidth(metrics.splitterHandle);

    setCentralWidget(m_splitter);

    // --- Строка состояния -------------------------------------------------------------
    //
    // Четыре секции, разделённые линиями в пиксель: состояние канала, счётчики, скорость
    // и ошибки, линии управления. Прежде всё это шло одной лентой цифр через точки, и
    // прочитать в ней «сколько принято» можно было только пересчитав пробелы.
    //
    // Состояние стоит слева и первым: это ответ на вопрос «что сейчас происходит», а
    // строка состояния — каноническое место для него. Состояния различаются формой
    // значка, поэтому цвет можно оставить одинаковым с остальными подписями.
    m_stateIndicator = new QLabel(this);
    m_stateLabel = new QLabel(this);
    // QStatusBar раскладывает добавленные виджеты по их собственной геометрии. Отступ
    // внутри QLabel не сдвигает сам индикатор, поэтому ставим отдельную распорку перед
    // ним. Шаг берётся из платформенной сетки, а не задаётся произвольным числом.
    auto *stateLeadingSpace = new QWidget(this);
    stateLeadingSpace->setFixedWidth(metrics.gap);
    statusBar()->addWidget(stateLeadingSpace);
    statusBar()->addWidget(m_stateIndicator);
    statusBar()->addWidget(m_stateLabel);

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    // Счётчики, скорость и время — моноширинным: иначе выравнивание пробелами ничего не
    // даёт, у пропорционального шрифта одинаковое число знаков занимает разную ширину, и
    // цифры дёргаются на каждом обновлении.
    m_statsLabel = new QLabel(this);
    m_statsLabel->setFont(mono);
    m_rateLabel = new QLabel(this);
    m_rateLabel->setFont(mono);
    m_uptimeLabel = new QLabel(this);
    m_uptimeLabel->setFont(mono);

    // Счётчик ошибок скрыт, пока ошибок нет. Постоянное «0 ошибок» — шум, который глаз
    // перестаёт замечать через минуту; появившаяся из ниоткуда надпись, наоборот, видна
    // сразу (эффект фон Ресторфа).
    m_errorsLabel = new QLabel(this);
    m_errorsLabel->setFont(mono);
    m_errorsLabel->hide();

    m_linesLabel = new QLabel(this);
    m_linesLabel->setFont(mono);

    statusBar()->addPermanentWidget(m_errorsLabel);
    statusBar()->addPermanentWidget(makeStatusSeparator());
    statusBar()->addPermanentWidget(m_linesLabel);
    statusBar()->addPermanentWidget(makeStatusSeparator());
    statusBar()->addPermanentWidget(m_uptimeLabel);
    statusBar()->addPermanentWidget(makeStatusSeparator());
    statusBar()->addPermanentWidget(m_rateLabel);
    statusBar()->addPermanentWidget(makeStatusSeparator());
    statusBar()->addPermanentWidget(m_statsLabel);

    // Время сеанса тикает само: остальные поля обновляются по событиям, а это — нет.
    m_uptimeTimer = new QTimer(this);
    m_uptimeTimer->setInterval(1000);
    connect(m_uptimeTimer, &QTimer::timeout, this, &MainWindow::updateUptime);

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

    // Вторая полоса управляет второй сессией теми же тремя сигналами. Настройки
    // интерфейса открываются тем же диалогом: устройство одно и то же, и второй диалог
    // для него был бы вторым местом правки одних настроек.
    if (m_secondSession) {
        connect(m_secondBar, &InterfaceBar::interfaceSelected, this, [this](const QString &id) {
            m_secondSession->setInterfaceId(id);
            if (!id.isEmpty())
                m_secondSession->open();
        });
        connect(m_secondBar, &InterfaceBar::toggleOpenRequested, this, [this] {
            if (m_secondSession->isActive())
                m_secondSession->close();
            else
                m_secondSession->open();
        });
        connect(m_secondBar, &InterfaceBar::settingsRequested, this,
                [this](const QString &id) { showInterfaceSettings(id); });
        connect(m_secondSession, &Session::stateChanged, this,
                [this](ChannelState state, const QString &detail) {
                    m_secondBar->setChannelState(state, detail);
                    updateSendAvailability();
                });
        connect(m_secondSession, &Session::errorOccurred, this, [this](const QString &message) {
            statusBar()->showMessage(message, 8000);
            m_secondBar->flagError(message);
        });
    }
    connect(m_interfaceBar, &InterfaceBar::settingsRequested, this, [this](const QString &id) {
        showInterfaceSettings(id);
    });

    connect(m_sendBar, &SendBar::sendRequested,
            this, [this](const QByteArray &data, SendBar::SendTarget target) {
                // «Первый доступный» разрешается здесь, а не в SendBar: только у окна
                // есть состояние сессий, чтобы решить, кто сейчас открыт (см.
                // updateSendAvailability()). A предпочитается B при прочих равных — тот
                // же порядок, в котором интерфейсы перечислены везде в интерфейсе.
                if (target == SendBar::SendTarget::FirstAvailable) {
                    const bool interfaceAOpen =
                        m_context.session && m_context.session->state() == ChannelState::Open;
                    target = interfaceAOpen ? SendBar::SendTarget::InterfaceA
                                            : SendBar::SendTarget::InterfaceB;
                }

                if (target == SendBar::SendTarget::InterfaceA
                    || target == SendBar::SendTarget::Both) {
                    if (m_context.session)
                        m_context.session->send(data);
                }
                if ((target == SendBar::SendTarget::InterfaceB
                     || target == SendBar::SendTarget::Both)
                    && m_secondSession) {
                    m_secondSession->send(data);
                }
            });

    // Просьба сохранить набранное рассылается всем хостам: кто умеет её принять, тот и
    // примет. Окно не знает, какая панель этим занимается, и знать не должно.
    connect(m_sendBar, &SendBar::makeMacroRequested, this,
            [this](const QString &text, int format, int termination) {
                for (PanelHostImpl *host : std::as_const(m_hosts)) {
                    host->notifySendBarSave(text, DataCodec::Format(format),
                                            DataCodec::Termination(termination));
                }
            });

    connect(m_sendBar, &SendBar::optionsChanged, this, [this] {
        m_settings.sendFormat = int(m_sendBar->format());
        m_settings.sendTermination = int(m_sendBar->termination());
        m_settings.save(*m_context.settings);
    });
    connect(m_sendBar, &SendBar::sendTargetChanged, this, [this](SendBar::SendTarget target) {
        m_settings.sendTarget = int(target);
        m_settings.save(*m_context.settings);
    });

    connect(m_terminal, &TerminalView::followTailChanged, this, [this](bool following) {
        m_followButton->setChecked(following);
    });

    // Кегль, подобранный колесом, переживает перезапуск: подбирают его один раз и надолго,
    // и терять его при закрытии окна значило бы заставлять делать это каждый сеанс.
    connect(m_terminal, &TerminalView::terminalFontSizeChanged, this, [this](int pointSize) {
        m_settings.fontSize = pointSize;
        m_settings.save(*m_context.settings);
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
    // Эхо — тоже про содержимое вывода, а не про действие над ним, поэтому кнопка стоит
    // в левой группе рядом с прочими переключателями показа. Прежде её не было вовсе:
    // настройка жила только в диалоге, хотя выключают её как раз по ходу работы — когда
    // устройство отвечает на каждую команду и собственные посылки удваивают вывод.
    m_echoButton = makeButton(tr("Echo sent data into the terminal"), true);
    m_lineNumberButton = makeButton(tr("Show line numbers"), true);
    m_csvFilterButton = makeButton(
        tr("Hide telemetry lines: values separated by the delimiter set in Settings. "
           "Right-click to change the delimiter"), true);
    m_hideUnreadableButton = makeButton(
        tr("Hide unreadable characters: control codes and invalid encoding. "
           "Right-click to choose how"),
        true);
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
    connect(m_echoButton, &QToolButton::toggled, this, [this](bool enabled) {
        if (m_context.session)
            m_context.session->setEchoEnabled(enabled);
        if (m_secondSession)
            m_secondSession->setEchoEnabled(enabled);
        m_settings.localEcho = enabled;
        m_settings.save(*m_context.settings);
    });
    connect(m_csvFilterButton, &QToolButton::toggled, this, [this](bool hide) {
        m_terminal->setCsvFilterEnabled(hide);
        m_settings.csvFilter = hide;
        m_settings.save(*m_context.settings);
    });

    // Разделитель подбирают под конкретную прошивку, и делают это по ходу работы — тем же
    // жестом, каким включают саму кнопку, а не походом в Settings ради одного символа. Поле
    // в диалоге остаётся: там его выставляют один раз и надолго, вместе с остальными
    // настройками терминала.
    m_csvFilterButton->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_csvFilterButton, &QToolButton::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        const QChar current = m_settings.csvSeparator.isEmpty()
                                  ? u',' : m_settings.csvSeparator.at(0);

        QMenu menu(m_csvFilterButton);
        auto *group = new QActionGroup(&menu);
        group->setExclusive(true);

        const QList<QPair<QString, QChar>> presets = {
            {tr("Comma (,)"), u','},
            {tr("Semicolon (;)"), u';'},
            {tr("Tab"), u'\t'},
            // Не просто "Space": то же слово в настройках UART означает чётность Space
            // (бит всегда 0), и общий словарь переводов сопоставляет строки без учёта
            // контекста tr() — общее слово увело бы перевод одного из двух не туда.
            {tr("Space character"), u' '},
            {tr("Pipe (|)"), u'|'},
        };
        for (const auto &[label, separator] : presets) {
            QAction *action = menu.addAction(label);
            action->setCheckable(true);
            action->setChecked(current == separator);
            action->setActionGroup(group);
            connect(action, &QAction::triggered, this, [this, separator] {
                applyCsvSeparator(separator);
            });
        }

        menu.addSeparator();
        connect(menu.addAction(tr("Custom…")), &QAction::triggered, this, [this] {
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, tr("Telemetry delimiter"), tr("Delimiter character:"),
                QLineEdit::Normal, m_settings.csvSeparator, &ok);
            if (ok && !text.isEmpty())
                applyCsvSeparator(text.at(0));
        });

        menu.exec(m_csvFilterButton->mapToGlobal(pos));
    });

    connect(m_hideUnreadableButton, &QToolButton::toggled, this, [this](bool hide) {
        m_terminal->setHideUnreadableEnabled(hide);
        m_settings.hideUnreadable = hide;
        m_settings.save(*m_context.settings);
    });

    // Способ показа подбирают по ходу работы, тем же жестом, каким подбирают разделитель
    // телеметрии: точка на месте байта годится, когда важно видеть, что он вообще был;
    // Hide вырезает байт из строки совсем, без следа и без сдвига остального текста в
    // отдельный столбец; целая строка — когда битый пакет изредка портит чистый текстовый
    // лог и его проще не видеть совсем.
    m_hideUnreadableButton->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_hideUnreadableButton, &QToolButton::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        const TerminalView::UnreadableMode current =
            unreadableModeFromName(m_settings.hideUnreadableMode);

        QMenu menu(m_hideUnreadableButton);
        auto *group = new QActionGroup(&menu);
        group->setExclusive(true);

        const QList<QPair<QString, TerminalView::UnreadableMode>> options = {
            {tr("Show as dots"), TerminalView::UnreadableMode::Dots},
            {tr("Hide"), TerminalView::UnreadableMode::Hide},
            {tr("Hide the whole line"), TerminalView::UnreadableMode::HideLine},
        };
        for (const auto &[label, mode] : options) {
            QAction *action = menu.addAction(label);
            action->setCheckable(true);
            action->setChecked(current == mode);
            action->setActionGroup(group);
            connect(action, &QAction::triggered, this, [this, mode] {
                m_settings.hideUnreadableMode = unreadableModeName(mode);
                m_terminal->setUnreadableMode(mode);
                m_settings.save(*m_context.settings);
            });
        }

        menu.exec(m_hideUnreadableButton->mapToGlobal(pos));
    });

    connect(m_lineNumberButton, &QToolButton::toggled, this, [this](bool show) {
        m_terminal->setShowLineNumbers(show);
        m_settings.showLineNumbers = show;
        m_settings.save(*m_context.settings);
    });
    // Пункт того же назначения есть в контекстном меню терминала. Кнопка обязана
    // отразить переключение оттуда, иначе она показывала бы одно, а терминал делал
    // другое. Сигналы кнопки при этом глушим: иначе она вернула бы значение обратно.
    connect(m_terminal, &TerminalView::showLineNumbersChanged, this, [this](bool show) {
        const QSignalBlocker blocker(m_lineNumberButton);
        m_lineNumberButton->setChecked(show);
        m_settings.showLineNumbers = show;
        m_settings.save(*m_context.settings);
    });
    connect(m_clearButton, &QToolButton::clicked, this, [this] {
        if (m_context.session)
            m_context.session->buffer()->clear();
    });
    connect(m_followButton, &QToolButton::toggled,
            m_terminal, &TerminalView::setFollowTail);

    // Две группы, разведённые растяжкой по краям полосы: слева переключатели того, **как**
    // показан вывод, справа действия **над** выводом. Прежде они стояли рядом и делились
    // рисованной линией — при пустой правой половине полосы это выглядело так, будто
    // кнопки просто не поместились. Растяжка разделяет их надёжнее любой линии
    // (гештальт-принцип близости) и заодно ставит частые действия к тому краю, куда рука
    // тянется за полосой прокрутки.
    //
    // «Очистить» — крайняя справа, как и просил владелец. Она единственная здесь
    // необратима, и место в углу отделяет её от переключателей, которые жмут не глядя.
    //
    // Режим области вывода — список, а не кнопки: три взаимоисключающих состояния читаются
    // списком быстрее, чем тремя переключателями, из которых нажат один. Стоит он в правой
    // группе, рядом с «Follow» и «Clear», а не среди переключателей показа: те читаются
    // слева направо как одна плотная группа однотипных кнопок (принцип подобия), а список —
    // это выбор области вывода, действие того же рода, что и «Follow»/«Clear», просто
    // сделанное не кнопкой. Полосы плагинов вставляют свои кнопки сразу за ним же, см.
    // showStripActions() — переезд combo их не трогает, там используется
    // indexOf(m_modeCombo), а не жёсткая позиция.
    m_modeCombo = new QComboBox(bar);
    m_modeCombo->addItem(tr("One interface"), QStringList{
                                                  QString::number(int(ViewMode::SingleTransport)),
                                                  QString()});
    m_modeCombo->addItem(tr("Two interfaces"), QStringList{
                                                   QString::number(int(ViewMode::DualTransport)),
                                                   QString()});
    m_modeCombo->setToolTip(tr("What the output area shows"));
    // Пункты для полос плагинов добавляются позже, в buildPanelPlugins(): к этому моменту
    // ни один плагин ещё не спрошен о том, что он объявляет.
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this] {
        const QStringList data = m_modeCombo->currentData().toStringList();
        if (data.size() == 2)
            applyViewMode(ViewMode(data.at(0).toInt()), data.at(1));
    });

    m_toolbarLayout = new QHBoxLayout(bar);
    QHBoxLayout *layout = m_toolbarLayout;
    layout->setContentsMargins(8, 3, 8, 3);
    // Тот же шаг, что и у остальных рядов с кнопками-значками в приложении — единый
    // ThemeMetrics::gap, а не свой отдельный номер (см. InterfaceBar.cpp). Один и тот же
    // промежуток везде в ряду, без «разделителя» между группами: addSpacing() добавлял бы
    // его поверх уже действующего setSpacing(), а не взамен.
    layout->setSpacing(ThemeManager::metrics().gap);
    layout->addWidget(m_hexButton);
    layout->addWidget(m_timestampButton);
    layout->addWidget(m_directionButton);
    layout->addWidget(m_echoButton);
    layout->addWidget(m_lineNumberButton);
    layout->addWidget(m_csvFilterButton);
    layout->addWidget(m_hideUnreadableButton);
    layout->addStretch(1);
    layout->addWidget(m_modeCombo);
    layout->addWidget(m_followButton);
    layout->addWidget(m_clearButton);

    return bar;
}

QWidget *MainWindow::buildSidePanel()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("sidePanel"));

    // Наименьшая ширина у стопки страниц, а не у панели целиком: свёрнутая панель — это
    // одна рейка, и предел, заданный панели, не дал бы ей сузиться до неё.
    auto *rail = new QWidget(panel);
    m_panelRail = rail;
    // Вертикальная рейка значков вместо полосы вкладок: она читается при любой ширине
    // панели и не вносит в оформление лишних рамок.
    //
    // Линия между рейкой и содержимым панели (правило #panelRail в QSS): без неё при
    // узкой панели столбец значков и её содержимое сливаются в один столбец кнопок.
    rail->setObjectName(QStringLiteral("panelRail"));
    m_railLayout = new QVBoxLayout(rail);
    m_railLayout->setContentsMargins(4, 6, 4, 6);
    m_railLayout->setSpacing(ThemeManager::metrics().gap);

    m_panelStack = new QStackedWidget(panel);
    m_panelStack->setMinimumWidth(kPanelStackMinWidth);

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

    // Каждая полоса плагина даёт свой пункт в переключателе режима: показать её вместо
    // терминала. Список строится из того, что объявили плагины, а не из зашитых названий.
    for (const PanelPluginRegistry::PanelEntry &entry : registry->panels()) {
        if (entry.descriptor.placement != PanelPlacement::Splitter
            || entry.descriptor.visibleByDefault) {
            continue;
        }
        m_modeCombo->addItem(entry.descriptor.title,
                             QStringList{QString::number(int(ViewMode::PluginStrip)),
                                         entry.descriptor.id});
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

    // Кнопка рейки и открывает панель, и закрывает её: повторное нажатие по уже открытой
    // сворачивает. Так рейка становится единственным местом, где панель переключают, —
    // не приходится помнить, что закрывают её где-то ещё.
    connect(button, &QToolButton::clicked, this, [this, index] {
        const bool wasCurrent = m_panelStack->currentIndex() == index;
        m_panelStack->setCurrentIndex(index);
        setSidePanelExpanded(!(wasCurrent && m_sidePanelExpanded));
    });

    m_railPanels.append({descriptor.id, widget, button});
}

void MainWindow::addSplitterPanel(const PanelDescriptor &descriptor, QWidget *widget)
{
    // Порядок внутри разделителя: всё, что «сверху», идёт до терминала, остальное после.
    // Индекс терминала ищется каждый раз заново — панелей может быть несколько, и он
    // сдвигается по мере их добавления.
    const int terminalIndex = m_terminalSplitter->indexOf(m_terminal);
    const int at = descriptor.side == PanelSide::Above ? terminalIndex : terminalIndex + 1;

    // Своей карточки у полосы нет: рамку даёт общая карточка области вывода, и вторая
    // внутри неё читалась бы как две границы подряд.
    m_terminalSplitter->insertWidget(at, widget);
    m_terminalSplitter->setStretchFactor(at, 0);

    // Скрытая по умолчанию полоса — это полоса, которую показывают переключателем режима,
    // а не постоянный житель окна. До этой правки поле PanelDescriptor::visibleByDefault
    // объявлялось в публичном ABI и не читалось никем.
    widget->setVisible(descriptor.visibleByDefault);
    m_splitterPanels.insert(descriptor.id, widget);
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

    // Боковая панель занимает треть окна, а нужна не всегда: при чтении длинных строк
    // терминалу требуется вся ширина. Свёрнутая панель оставляет рейку значков — и
    // потому, что через неё панель возвращают, и потому, что исчезающий вместе с панелью
    // столбец кнопок сдвигал бы всё окно при каждом переключении.
    auto *sidePanelAction = viewMenu->addAction(tr("Side &panel"));
    sidePanelAction->setCheckable(true);
    sidePanelAction->setChecked(true);
    connect(sidePanelAction, &QAction::toggled, this,
            [this](bool expanded) { setSidePanelExpanded(expanded); });
    m_actions.insert(QStringLiteral("view.sidePanel"), sidePanelAction);

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
    // Пустое значение означает «шрифт по умолчанию», и по умолчанию теперь встроенный:
    // одинаковая картинка вывода на всех трёх системах и глифы Nerd Font, которых нет ни
    // в одном системном моноширинном. Системный остаётся запасным путём — если ресурс не
    // загрузился, терминал должен работать, просто выглядеть иначе.
    QFont font;
    if (!m_settings.fontFamily.isEmpty()) {
        font = QFont(m_settings.fontFamily);
    } else if (const QString embedded = EmbeddedFonts::monospaceFamily(); !embedded.isEmpty()) {
        font = QFont(embedded);
    } else {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    if (m_settings.fontSize > 0)
        font.setPointSize(m_settings.fontSize);
    m_terminal->setTerminalFont(font);

    m_terminal->setShowTimestamps(m_settings.showTimestamps);
    m_terminal->setRelativeTimestamps(m_settings.relativeTimestamps);
    m_terminal->setTimestampFormat(m_settings.timestampFormat);
    m_terminal->setShowDirection(m_settings.showDirection);
    m_terminal->setShowLineNumbers(m_settings.showLineNumbers);
    m_terminal->setCsvSeparator(m_settings.csvSeparator.isEmpty()
                                    ? u','
                                    : m_settings.csvSeparator.at(0));
    m_terminal->setCsvFilterEnabled(m_settings.csvFilter);
    m_terminal->setUnreadableMode(unreadableModeFromName(m_settings.hideUnreadableMode));
    m_terminal->setHideUnreadableEnabled(m_settings.hideUnreadable);
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
    const QSignalBlocker echoBlocker(m_echoButton);
    const QSignalBlocker numbersBlocker(m_lineNumberButton);
    const QSignalBlocker csvBlocker(m_csvFilterButton);
    const QSignalBlocker unreadableBlocker(m_hideUnreadableButton);
    m_hexButton->setChecked(m_settings.viewMode == QLatin1String("hex"));
    m_timestampButton->setChecked(m_settings.showTimestamps);
    m_directionButton->setChecked(m_settings.showDirection);
    m_echoButton->setChecked(m_settings.localEcho);
    m_lineNumberButton->setChecked(m_settings.showLineNumbers);
    m_csvFilterButton->setChecked(m_settings.csvFilter);
    m_hideUnreadableButton->setChecked(m_settings.hideUnreadable);

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
    if (m_secondSession)
        m_secondSession->setEchoEnabled(m_settings.localEcho);

    m_sendBar->setFormat(DataCodec::Format(m_settings.sendFormat));
    m_sendBar->setTermination(DataCodec::Termination(m_settings.sendTermination));
    m_sendBar->setSendTarget(SendBar::SendTarget(m_settings.sendTarget));

    applyShortcuts();
}

void MainWindow::applyCsvSeparator(QChar separator)
{
    m_settings.csvSeparator = QString(separator);
    m_terminal->setCsvSeparator(separator);
    m_settings.save(*m_context.settings);
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

    // Состояния различаются формой значка, а не цветом: строка состояния остаётся
    // спокойной и читаемой на чёрно-белом снимке или при нарушении цветового зрения.
    if (m_context.theme) {
        const ThemeColors &colors = m_context.theme->colors();
        QString name;
        char32_t glyph = mdi::CircleOutline;
        switch (state) {
        case ChannelState::Open:
            name = tr("Open");
            glyph = mdi::CheckboxBlankCircle;
            break;
        case ChannelState::Opening:
            name = tr("Opening");
            break;
        case ChannelState::Closed:
            name = tr("Closed");
            break;
        case ChannelState::Unavailable:
            name = tr("Unavailable");
            glyph = mdi::AlertCircleOutline;
            break;
        case ChannelState::Error:
            name = tr("Error");
            glyph = mdi::AlertCircleOutline;
            break;
        }
        m_stateIndicator->setPixmap(
            MdiIcons::icon(glyph, colors.textMuted, kStatusGlyphSize).pixmap(kStatusGlyphSize));
        m_stateLabel->setText(name);
        m_stateLabel->setToolTip(detail);
        m_stateIndicator->setToolTip(detail);
    }

    // Счётчик времени идёт, только пока канал открыт: у закрытого канала «время сеанса»
    // означало бы время, прошедшее с закрытия, а это другое число.
    if (open) {
        if (!m_openedAt.isValid()) {
            m_openedAt = QDateTime::currentDateTime();
            m_uptimeTimer->start();
        }
    } else {
        m_openedAt = QDateTime();
        m_uptimeTimer->stop();
        m_uptimeLabel->clear();
    }
    updateUptime();
    updateSendAvailability();
    updatePlaceholder(state);

    // Панели узнают о состоянии через свои хосты: окну незачем знать, какая из них что
    // из него извлечёт.
    for (PanelHostImpl *host : std::as_const(m_hosts))
        host->notifyChannelState(state);

    if (open)
        m_sendBar->focusInput();
}

void MainWindow::applyViewMode(ViewMode mode, const QString &stripId)
{
    const bool dual = mode == ViewMode::DualTransport;
    const bool strip = mode == ViewMode::PluginStrip;
    m_dualTransport = dual;
    m_sendBar->setDualTransport(dual);

    m_secondBarCard->setVisible(dual);

    if (dual) {
        // Буфер A становится общим, а не заменяется новым. Иначе при переходе к двум
        // интерфейсам история A исчезала с экрана, хотя пользователь не просил её
        // очистить. Запасной буфер нужен только для неполного AppContext в тестах.
        TerminalBuffer *sharedBuffer = m_context.session ? m_context.session->buffer()
                                                         : m_sharedBuffer;
        if (m_context.session)
            m_context.session->setSharedBuffer(sharedBuffer, 0);
        if (m_secondSession)
            m_secondSession->setSharedBuffer(sharedBuffer, 1);
        m_terminal->setBuffer(sharedBuffer);
        m_terminal->setShowSource(true);
    } else {
        // Возврат к одному транспорту закрывает второй канал: оставить его открытым,
        // спрятав полосу выбора, значило бы держать занятым порт, о котором на экране
        // нет ни следа.
        if (m_secondSession) {
            m_secondSession->close();
            m_secondSession->setSharedBuffer(nullptr, 0);
        }
        if (m_context.session) {
            m_context.session->setSharedBuffer(nullptr, 0);
            m_terminal->setBuffer(m_context.session->buffer());
        }
        m_terminal->setShowSource(false);
    }

    // Полоса плагина занимает место терминала. Показывается ровно одна — та, что выбрана;
    // остальные прячутся, иначе переключение между двумя графиками накапливало бы их в
    // окне одну под другой.
    QWidget *shown = nullptr;
    for (auto it = m_splitterPanels.cbegin(); it != m_splitterPanels.cend(); ++it) {
        const bool visible = strip && it.key() == stripId;
        it.value()->setVisible(visible);
        if (visible)
            shown = it.value();
    }
    m_terminal->setVisible(!strip);

    // Переключатели показа вывода относятся к терминалу и в режиме полосы бессмысленны.
    // Оставить их доступными значило бы обещать действия, которых не произойдёт.
    for (QToolButton *button : {m_hexButton, m_timestampButton, m_directionButton,
                                m_echoButton, m_lineNumberButton, m_csvFilterButton,
                                m_hideUnreadableButton, m_followButton, m_clearButton}) {
        button->setVisible(!strip);
    }

    showStripActions(shown);

    m_context.settings->setValue(QLatin1String(kKeyViewMode), int(mode));
    m_context.settings->setValue(QLatin1String(kKeyViewStrip), stripId);
    updateSendAvailability();
}

void MainWindow::updateSendAvailability()
{
    const bool interfaceA = m_context.session
                            && m_context.session->state() == ChannelState::Open;
    const bool interfaceB = m_secondSession
                            && m_secondSession->state() == ChannelState::Open;
    m_sendBar->setInterfaceAvailability(interfaceA, m_dualTransport && interfaceB);
}

void MainWindow::showStripActions(QWidget *strip)
{
    // Кнопки полосы берутся из её собственных QAction. Так окно не знает ни о графике, ни
    // о любом другом плагине: виджет объявляет, чем им управляют, а панель это показывает.
    // Заводить ради этого метод в IPanelHost незачем — QWidget::actions() уже есть.
    qDeleteAll(m_stripButtons);
    m_stripButtons.clear();

    if (!strip)
        return;

    // Место — сразу за переключателем режима: это управление тем, что показано, и стоять
    // оно должно рядом с выбором того, что показано. Позиция считается один раз и растёт
    // с каждой кнопкой: вставка всех по одному и тому же индексу перевернула бы порядок,
    // объявленный полосой, — пауза оказалась бы после очистки.
    int at = m_toolbarLayout->indexOf(m_modeCombo) + 1;

    for (QAction *action : strip->actions()) {
        auto *button = new QToolButton(m_toolbarLayout->parentWidget());
        button->setAutoRaise(true);
        button->setDefaultAction(action);
        button->setIconSize(QSize(kToolGlyphSize, kToolGlyphSize));
        // Полоса управления узкая, и подпись рядом со значком её распирает; значок с
        // подсказкой — тот же вид, что у остальных кнопок панели.
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_toolbarLayout->insertWidget(at++, button);
        m_stripButtons.append(button);
    }
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

QWidget *MainWindow::makeStatusSeparator()
{
    // Линия, а не пробел: секции строки состояния — разные виды сведений, и пробелом
    // отделить их от соседних цифр невозможно, всё сливается в одну ленту.
    //
    // QFrame::VLine здесь не годится по той же причине, что и в панели терминала: он
    // рисуется палитрой стиля, а не цветом темы, и на тёмной теме даёт вдавленную
    // двухцветную канавку вместо линии.
    auto *line = new QFrame(this);
    line->setObjectName(QStringLiteral("statusSeparator"));
    line->setFixedWidth(1);
    return line;
}

void MainWindow::updateStatistics()
{
    if (!m_context.session) {
        m_statsLabel->clear();
        m_rateLabel->clear();
        m_errorsLabel->hide();
        return;
    }

    const Session::Statistics stats = m_context.session->statistics();

    // Значения дополнены пробелами до постоянной ширины: без этого счётчики меняют ширину
    // на каждом обновлении и сдвигают всё, что стоит правее, по нескольку раз в секунду.
    m_statsLabel->setText(tr("RX %1   TX %2")
                              .arg(Formatting::byteCount(stats.bytesReceived)
                                       .rightJustified(kStatsValueWidth),
                                   Formatting::byteCount(stats.bytesSent)
                                       .rightJustified(kStatsValueWidth)));

    // Скорость показывается, только пока данные идут: «0 Б/с» на простое означает ровно
    // то же, что пустое место, но занимает внимание.
    m_rateLabel->setText(
        stats.receiveRateBps > 0.5
            ? tr("%1/s").arg(Formatting::byteCount(qint64(stats.receiveRateBps)))
                  .rightJustified(kStatsRateWidth)
            : QString(kStatsRateWidth, u' '));

    // Ошибки появляются из ниоткуда и потому заметны. Постоянное «0 ошибок» глаз
    // перестаёт замечать через минуту — а именно его и нужно было бы заметить.
    if (stats.errorCount > 0) {
        m_errorsLabel->setText(tr("%n error(s)", nullptr, int(stats.errorCount)));
        m_errorsLabel->show();
    } else {
        m_errorsLabel->hide();
    }
}

void MainWindow::updateUptime()
{
    if (!m_openedAt.isValid()) {
        m_uptimeLabel->clear();
        return;
    }

    const qint64 seconds = m_openedAt.secsTo(QDateTime::currentDateTime());
    m_uptimeLabel->setText(QStringLiteral("%1:%2:%3")
                               .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
                               .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                               .arg(seconds % 60, 2, 10, QLatin1Char('0')));
    m_uptimeLabel->setToolTip(tr("Time since the interface was opened"));
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
    m_echoButton->setIcon(MdiIcons::icon(mdi::Keyboard, kToolGlyphSize));
    m_lineNumberButton->setIcon(MdiIcons::icon(mdi::FormatListNumbered, kToolGlyphSize));
    m_csvFilterButton->setIcon(MdiIcons::icon(mdi::TableOff, kToolGlyphSize));
    m_hideUnreadableButton->setIcon(MdiIcons::icon(mdi::ImageBrokenVariant, kToolGlyphSize));
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

    // Видимость боковой панели восстанавливается до её содержимого: скрытая панель не
    // должна мигнуть на экране при запуске.
    // Ширина читается до состояния: свёрнутая панель к ней не обратится, зато первое же
    // разворачивание вернёт именно ту, что выбрал пользователь, а не умолчание.
    m_sidePanelWidth = store->value(QLatin1String(kKeySidePanelWidth), 0).toInt();
    if (m_sidePanelWidth <= 0 && !m_splitter->sizes().isEmpty())
        m_sidePanelWidth = m_splitter->sizes().first();

    const bool sidePanelExpanded = store->value(QLatin1String(kKeySidePanel), true).toBool();
    setSidePanelExpanded(sidePanelExpanded);

    // Режим области вывода восстанавливается после того, как построены полосы плагинов:
    // до этого выбирать было бы не из чего.
    const int savedMode = store->value(QLatin1String(kKeyViewMode),
                                       int(ViewMode::SingleTransport)).toInt();
    const QString savedStrip = store->value(QLatin1String(kKeyViewStrip)).toString();
    for (int i = 0; i < m_modeCombo->count(); ++i) {
        const QStringList data = m_modeCombo->itemData(i).toStringList();
        if (data.size() == 2 && data.at(0).toInt() == savedMode && data.at(1) == savedStrip) {
            m_modeCombo->setCurrentIndex(i);
            break;
        }
    }
    applyViewMode(ViewMode(savedMode), savedStrip);

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
    // уже выбранная в buildPanelPlugins(). Именно selectRailPanel(), а не activatePanel():
    // последний разворачивает панель, и свёрнутая при выходе раскрывалась бы при каждом
    // запуске.
    if (!panelId.isEmpty())
        selectRailPanel(panelId);
}

bool MainWindow::selectRailPanel(const QString &panelId)
{
    for (int i = 0; i < m_railPanels.size(); ++i) {
        if (m_railPanels.at(i).id != panelId)
            continue;
        m_railPanels.at(i).button->setChecked(true);
        m_panelStack->setCurrentIndex(i);
        return true;
    }
    return false;
}

void MainWindow::activatePanel(const QString &panelId)
{
    // Выбор страницы и разворачивание разделены намеренно. Плагин, просящий показать свою
    // панель, вправе её раскрыть — иначе «Найти» переключало бы невидимую страницу и
    // выглядело неработающим. Восстановление состояния при запуске такого права не имеет:
    // оно лишь возвращает выбранную страницу, и свёрнутая панель обязана остаться
    // свёрнутой.
    if (selectRailPanel(panelId))
        setSidePanelExpanded(true);
}

bool MainWindow::applySidePanelGeometry()
{
    const int railWidth = m_panelRail->sizeHint().width();

    if (m_sidePanelExpanded) {
        m_sidePanel->setMinimumWidth(0);
        m_sidePanel->setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        // Ширина рейки закрепляется с обеих сторон: иначе свёрнутую панель можно
        // растянуть мышью, и рядом со столбцом значков появится пустая полоса —
        // состояние, которого в интерфейсе нет.
        m_sidePanel->setMinimumWidth(railWidth);
        m_sidePanel->setMaximumWidth(railWidth);
    }

    // Остаток считается от действительной ширины разделителя, а не задаётся единицей в
    // расчёте на растяжение: избыток QSplitter раздаёт по самим размерам, а не по
    // коэффициентам, и правая колонка получила бы лишь долю освободившегося места.
    //
    // До первого показа окна width() ничего не значит — там ещё размер по умолчанию, а не
    // восстановленный. Раскладку в этом случае не трогаем вовсе: её доделает showEvent().
    // Прежде она считалась именно тогда, и запомненная ширина панели появлялась только
    // после первого переключения.
    const int total = m_splitter->width() - m_splitter->handleWidth();
    if (!isVisible() || total <= railWidth)
        return false;

    const int width = m_sidePanelExpanded
        ? qBound(railWidth + m_panelStack->minimumWidth(),
                 m_sidePanelWidth > 0 ? m_sidePanelWidth : kDefaultSidePanelWidth,
                 qMax(railWidth + 1, total / 2))
        : railWidth;
    m_splitter->setSizes({width, total - width});
    return true;
}

void MainWindow::setSidePanelExpanded(bool expanded)
{
    const bool changed = m_sidePanelExpanded != expanded;

    // Ширину запоминаем до сворачивания: после него разделитель знает только ширину рейки.
    if (!expanded && m_sidePanelExpanded) {
        const QList<int> sizes = m_splitter->sizes();
        if (!sizes.isEmpty() && sizes.first() > m_panelRail->sizeHint().width())
            m_sidePanelWidth = sizes.first();
    }

    m_sidePanelExpanded = expanded;
    m_panelStack->setVisible(expanded);
    applySidePanelGeometry();

    // Кнопка открытой страницы остаётся нажатой и в свёрнутом виде: она показывает, куда
    // вернёт следующее нажатие. Пустая рейка об этом не говорит ничем.
    if (!changed)
        return;

    if (QAction *action = m_actions.value(QStringLiteral("view.sidePanel"))) {
        const QSignalBlocker blocker(action);
        action->setChecked(expanded);
    }

    m_context.settings->setValue(QLatin1String(kKeySidePanel), expanded);
    m_context.settings->setValue(QLatin1String(kKeySidePanelWidth), m_sidePanelWidth);
}

void MainWindow::composeInSendBar(const QString &text, DataCodec::Format format)
{
    m_sendBar->setFormat(format);
    m_sendBar->setText(text);
    m_sendBar->focusInput();
}

DataCodec::Termination MainWindow::sendTermination() const
{
    return m_sendBar ? m_sendBar->termination() : DataCodec::Termination::None;
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

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    // Только на первом показе: дальше состояние ведут кнопки рейки и разделитель, и
    // повторное применение затирало бы ширину, которую пользователь только что выбрал
    // мышью, при каждом сворачивании окна.
    if (m_sidePanelGeometryApplied)
        return;
    // Признак ставится по факту применения, а не по факту показа: если ширины ещё нет,
    // раскладка обязана дождаться следующего показа, а не считаться выполненной.
    m_sidePanelGeometryApplied = applySidePanelGeometry();
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
