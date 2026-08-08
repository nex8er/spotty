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

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QStringList>

class QAction;
class QButtonGroup;
class QLabel;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace spotty {

struct PanelDescriptor;
class InterfaceBar;
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
    TerminalView *m_terminal = nullptr;
    SendBar *m_sendBar = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_sidePanel = nullptr; ///< Боковая панель целиком: рейка и стопка страниц.
    QStackedWidget *m_panelStack = nullptr;
    QVBoxLayout *m_railLayout = nullptr; ///< Раскладка рейки; кнопки вставляются в неё.
    QToolButton *m_settingsRailButton = nullptr; ///< Открывает диалог настроек; внизу рейки.

    /// \brief Вертикальный разделитель: полосы плагинов и сам терминал.
    QSplitter *m_terminalSplitter = nullptr;

    /// \brief Карточка терминала внутри разделителя; относительно неё встают полосы.
    QWidget *m_terminalCard = nullptr;

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
    QToolButton *m_clearButton = nullptr;
    QToolButton *m_followButton = nullptr;

    QLabel *m_statsLabel = nullptr;
    QLabel *m_linesLabel = nullptr;

    /// \brief Действия по идентификаторам из spotty::SettingsDialog::shortcutActions().
    QHash<QString, QAction *> m_actions;

    /// \brief Буфер под просматриваемый файл лога; живой вывод хранится отдельно.
    TerminalBuffer *m_logBuffer = nullptr;

    QWidget *m_logViewBar = nullptr;
    QLabel *m_logViewLabel = nullptr;
};

} // namespace spotty
