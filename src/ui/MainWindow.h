/**
 * \file MainWindow.h
 * \brief Главное окно приложения.
 */
#pragma once

#include "AppContext.h"
#include "TerminalView.h"
#include "theme/ThemeManager.h"

#include "dialogs/SettingsDialog.h"

#include <settings/AppSettings.h>
#include <spotty/api/ChannelState.h>
#include <spotty/data/DataCodec.h>

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QStringList>

class QAction;
class QButtonGroup;
class QLabel;
class QComboBox;
class QSplitter;
class QTimer;
class QStackedWidget;
class QToolButton;
class QHBoxLayout;
class QVBoxLayout;

namespace spotty {

struct PanelDescriptor;
class InterfaceBar;
class Session;
class OverlayLayer;
class PanelHostImpl;
class SendBar;

/**
 * \class MainWindow
 * \brief Главное окно: постоянный каркас, применение настроек и состояние окна.
 *
 * \par Раскладка
 *
 * \verbatim
 *  ┌───────────────────────────────────────────────────────┐
 *  │ InterfaceBar: состояние · выбор интерфейса · настройки │
 *  ├───────────────────────────────────────────────────────┤
 *  │ панель терминала: текст/HEX · метки · очистка · слежение│
 *  ├──────────────────────────────────┬────────────────────┤
 *  │        TerminalView              │ ▌ рейка · панели   │
 *  ├──────────────────────────────────┴────────────────────┤
 *  │ SendBar: ввод · формат · терминация · Отправить       │
 *  ├───────────────────────────────────────────────────────┤
 *  │ строка состояния: счётчики, скорость, линии управления │
 *  └───────────────────────────────────────────────────────┘
 * \endverbatim
 *
 * \par Настройки
 *
 * Окно держит копию spotty::AppSettings и раздаёт её потребителям одним методом
 * applySettings(). Это единственное место, где настройка превращается в действие, поэтому
 * добавление новой сводится к одной строке здесь и одной в структуре.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const AppContext &context, QWidget *parent = nullptr);

    /// \name Службы для панельных плагинов; зовёт spotty::PanelHostImpl
    /// @{

    TerminalView *terminalView() const { return m_terminal; }

    /// \brief Сделать панель видимой и выбранной. Неизвестный идентификатор игнорируется.
    void activatePanel(const QString &panelId);

    /// \brief Положить текст в строку отправки, не отправляя.
    void composeInSendBar(const QString &text, DataCodec::Format format);

    void showStatusMessage(const QString &message);

    /// \brief Показать файл в области терминала вместо живого вывода.
    bool showDocument(const QString &filePath, const QString &title);

    /// @}

public Q_SLOTS:
    /// \brief Показать и поднять окно. Вызывается вторым экземпляром приложения.
    void raiseWindow();

protected:
    /// \brief Сохраняет состояние окна перед закрытием.
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildMenus();

    /// \brief Панель управления показом терминала.
    QWidget *buildTerminalToolbar();

    /// \brief Боковая панель слева: рейка значков и стопка страниц.
    QWidget *buildSidePanel();

    /**
     * \brief Создать хосты и панели всех загруженных панельных плагинов.
     *
     * Вызывается после того, как построены терминал и строка отправки: хост в
     * конструкторе подписывается на их сигналы.
     */
    void buildPanelPlugins();

    /// \brief Врезать звенья цепочки преобразования, объявленные плагинами.
    void installPluginFilters();

    /// \brief Добавить страницу в рейку значков.
    void addRailPanel(const PanelDescriptor &descriptor, QWidget *widget, QButtonGroup *group);

    /// \brief Добавить полосу над или под терминалом.
    void addSplitterPanel(const PanelDescriptor &descriptor, QWidget *widget);

    /**
     * \brief Развернуть или свернуть боковую панель.
     * \param expanded Показать содержимое панели рядом с рейкой.
     *
     * Свёрнутая панель — это по-прежнему видимая рейка значков: скрывать её целиком
     * значило бы прятать и единственный способ вернуть панель обратно. Ширина развёрнутой
     * панели запоминается и восстанавливается.
     */
    void setSidePanelExpanded(bool expanded);

    /**
     * \brief Выбрать страницу рейки, не трогая свёрнутость панели.
     * \return `false`, если панели с таким идентификатором нет.
     *
     * Отделено от activatePanel() ради восстановления состояния: оно возвращает выбранную
     * страницу, но не вправе раскрывать панель, свёрнутую пользователем перед выходом.
     */
    bool selectRailPanel(const QString &panelId);

    /// \brief Обернуть содержимое в карточку с рамкой — общий вид всех блоков окна.
    QWidget *makeCard(QWidget *content);

    /// \brief Раздать текущие настройки всем потребителям.
    void applySettings();

    /// \brief Назначить действиям сочетания клавиш из настроек.
    void applyShortcuts();

    /// \brief Действия окна вместе с сочетаниями, объявленными панелями.
    QList<ShortcutAction> allShortcutActions() const;

    /// \brief Текущие настройки панельных плагинов по их идентификаторам.
    QHash<QString, QVariantMap> currentPluginSettings() const;

    /// \brief Показать диалог настроек и применить результат.
    void showSettingsDialog();

    /**
     * \brief Стереть настройки, реестр интерфейсов и историю отправки, вернуть умолчания.
     *
     * Подключается к SettingsDialog::resetToDefaultsRequested() — тому окну не принадлежат
     * ни реестр интерфейсов, ни история отправки, поэтому сам сброс делает MainWindow,
     * владеющий m_context целиком.
     */
    void resetToDefaults();

    /// \brief Восстановить геометрию окна и положение разделителя.
    void restoreWindowState();

    /// \brief Записать состояние окна в настройки.
    void persistWindowState();

    /// \brief Пересобрать раскрашенные значки. Вызывается при смене темы.
    void updateIcons();

    /// \brief Открыть или закрыть канал в зависимости от текущего состояния.
    void toggleConnection();

    /**
     * \brief Показать настройки интерфейса с заранее выбранным устройством.
     * \param interfaceId Устройство, которое нужно выбрать в списке.
     * \param invalidFields Ключи полей схемы, которые нужно подсветить как ошибочные
     *        (см. spotty::InterfaceSettingsPanel::flagInvalidFields()) — приходят из
     *        Session::requiredSettingsMissing(), когда пришли не сразу после клика по
     *        кнопке настроек, а после неудачной попытки открыть канал.
     */
    void showInterfaceSettings(const QString &interfaceId, const QStringList &invalidFields = {});

    /**
     * \brief Перечитать настройки сессии, если только что применённые относились к её
     *        интерфейсу.
     * \param interfaceId Устройство, чьи настройки в реестре только что изменились.
     *
     * Подключается к spotty::InterfaceSettingsPanel::settingsApplied() из обоих мест, где
     * панель встречается, — кнопки настроек рядом с выбором интерфейса и раздела
     * «Интерфейсы» в общих настройках.
     */
    void reloadSessionSettingsIfActive(const QString &interfaceId);

    /// \brief Отразить новое состояние канала во всех элементах окна.
    void applyChannelState(ChannelState state, const QString &detail);

    /**
     * \enum ViewMode
     * \brief Что занимает область вывода.
     */
    enum class ViewMode {
        SingleTransport = 0, ///< Один интерфейс — как было всегда.
        DualTransport,       ///< Два интерфейса в общий буфер, строки помечены A и B.

        /**
         * \brief Вместо терминала — полоса плагина.
         *
         * Пунктов этого рода в списке столько, сколько плагинов объявило полосу в
         * разделителе; сегодня это график. Жёстко назвать здесь «график» значило бы
         * вписать в окно знание о конкретном плагине — ровно то, от чего ушли, переводя
         * панели в плагины.
         */
        PluginStrip,
    };

    /// \brief Переключить режим области вывода.
    /// \param stripId Идентификатор полосы для ViewMode::PluginStrip.
    void applyViewMode(ViewMode mode, const QString &stripId = {});

    /**
     * \brief Показать в панели управления кнопки активной полосы плагина.
     * \param strip Полоса либо `nullptr`, чтобы убрать прежние.
     *
     * Кнопки берутся из QWidget::actions() самой полосы: так окну не нужно знать ни о
     * графике, ни о любом другом плагине.
     */
    void showStripActions(QWidget *strip);

    /**
     * \brief Обновить подсказку на месте пустого вывода терминала.
     * \param state Состояние канала.
     *
     * Живёт здесь, а не в TerminalView: сам терминал знает только, что строк нет, а
     * почему их нет — вопрос к сессии и к тому, не открыт ли сейчас файл лога.
     */
    void updatePlaceholder(ChannelState state);

    void updateStatistics();
    void updateControlLines(const QVariantMap &lines);

    /**
     * \brief Показать файл лога в области терминала.
     *
     * Живой вывод при этом не теряется: он остаётся в своём буфере и возвращается на
     * экран кнопкой «к живому выводу». Показывать лог в том же буфере значило бы
     * затереть то, что пришло с устройства.
     *
     * \return `false`, если файл не удалось прочитать.
     */
    bool showLogFile(const QString &filePath);

    /// \brief Вернуть в область терминала живой вывод сессии.
    void returnToLiveView();

    AppContext m_context;
    AppSettings m_settings;

    InterfaceBar *m_interfaceBar = nullptr;

    /// \name Второй транспорт
    /// Появляется, когда выбран режим двух интерфейсов. Сессия создаётся сразу, а полоса
    /// выбора прячется: держать вторую сессию наготове дешевле, чем создавать её на
    /// переключении режима и заново связывать все сигналы.
    /// @{
    InterfaceBar *m_secondBar = nullptr;
    Session *m_secondSession = nullptr;

    /**
     * \brief Общий буфер обоих транспортов.
     *
     * Владеет им окно, а не сессия: буфер переживает переключение режима, и обе сессии
     * ссылаются на него, помечая свои строки номером источника.
     */
    TerminalBuffer *m_sharedBuffer = nullptr;

    QComboBox *m_modeCombo = nullptr;

    /// \brief Карточка второй полосы выбора; прячется вместе с ней.
    QWidget *m_secondBarCard = nullptr;

    /// \brief Карточки полос, объявленных плагинами, по идентификатору панели.
    QHash<QString, QWidget *> m_splitterPanels;
    /// @}
    TerminalView *m_terminal = nullptr;
    SendBar *m_sendBar = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_sidePanel = nullptr; ///< Боковая панель целиком: рейка и стопка страниц.
    QWidget *m_panelRail = nullptr; ///< Столбец значков; остаётся видимым и в свёрнутом виде.
    QStackedWidget *m_panelStack = nullptr;
    QVBoxLayout *m_railLayout = nullptr; ///< Раскладка рейки; кнопки вставляются в неё.

    /// \brief Развёрнута ли боковая панель. Свёрнутая — это одна рейка значков.
    bool m_sidePanelExpanded = true;

    /**
     * \brief Ширина развёрнутой панели, выбранная пользователем.
     *
     * Хранится отдельно от состояния разделителя: в свёрнутом виде разделитель знает лишь
     * ширину рейки, и сохранись только он — выбранная ширина терялась бы при каждом
     * закрытии панели.
     */
    int m_sidePanelWidth = 0;
    QToolButton *m_settingsRailButton = nullptr; ///< Открывает диалог настроек; внизу рейки.

    /// \brief Вертикальный разделитель: полосы плагинов и сам терминал.
    QSplitter *m_terminalSplitter = nullptr;

    /// \brief Кнопки, пришедшие от показанной полосы плагина; чистятся при смене режима.
    QList<QToolButton *> m_stripButtons;

    /// \brief Куда они вставляются в панели управления.
    QHBoxLayout *m_toolbarLayout = nullptr;

    /// \brief Слой поверх области вывода; ребёнок viewport терминала.
    OverlayLayer *m_overlay = nullptr;

    /**
     * \struct RailPanel
     * \brief Страница рейки вместе с её кнопкой.
     *
     * Прежде порядок панелей задавали две параллельные константы, а адресовались они
     * числовым индексом — меню переключало страницу вызовом setCurrentIndex(2). С
     * плагинами набор перестал быть постоянным, и любой такой номер означал бы разное при
     * разном наборе установленных плагинов.
     */
    struct RailPanel
    {
        QString id;
        QWidget *widget = nullptr;
        QToolButton *button = nullptr;
    };
    QList<RailPanel> m_railPanels;

    /// \brief Хосты панельных плагинов по идентификатору плагина. Окно ими владеет.
    QHash<QString, PanelHostImpl *> m_hosts;

    QToolButton *m_hexButton = nullptr;
    QToolButton *m_timestampButton = nullptr;
    QToolButton *m_directionButton = nullptr;
    QToolButton *m_echoButton = nullptr;
    QToolButton *m_lineNumberButton = nullptr;
    QToolButton *m_csvFilterButton = nullptr;
    QToolButton *m_clearButton = nullptr;
    QToolButton *m_followButton = nullptr;

    /// \name Строка состояния
    /// Секции разделены линиями, а не пробелами: сплошная лента цифр не читается, а
    /// группировка близостью работает без единого пикселя разметки.
    /// @{
    QLabel *m_stateLabel = nullptr;   ///< Состояние канала: цвет плюс слово.
    QLabel *m_statsLabel = nullptr;   ///< Счётчики приёма и передачи.
    QLabel *m_rateLabel = nullptr;    ///< Скорость приёма; пусто, когда данных нет.
    QLabel *m_errorsLabel = nullptr;  ///< Счётчик ошибок; скрыт, пока их нет.
    QLabel *m_uptimeLabel = nullptr;  ///< Сколько открыт текущий сеанс.
    QLabel *m_linesLabel = nullptr;   ///< Линии управления.
    /// @}

    /// \brief Момент открытия канала для счётчика времени сеанса.
    QDateTime m_openedAt;

    /// \brief Тикает раз в секунду, пока канал открыт.
    QTimer *m_uptimeTimer = nullptr;

    /// \brief Собрать разделитель секций строки состояния.
    QWidget *makeStatusSeparator();

    /// \brief Обновить надпись о длительности сеанса.
    void updateUptime();

    /// \brief Действия по идентификаторам из spotty::SettingsDialog::shortcutActions().
    QHash<QString, QAction *> m_actions;

    /// \brief Буфер под просматриваемый файл лога; живой вывод хранится отдельно.
    TerminalBuffer *m_logBuffer = nullptr;

    QWidget *m_logViewBar = nullptr;
    QLabel *m_logViewLabel = nullptr;
};

} // namespace spotty
