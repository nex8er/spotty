/**
 * \file MainWindow.h
 * \brief Главное окно приложения.
 */
#pragma once

#include "AppContext.h"
#include "TerminalView.h"
#include "theme/ThemeManager.h"

#include <settings/AppSettings.h>
#include <spotty/api/ChannelState.h>

#include <QHash>
#include <QMainWindow>

class QAction;
class QLabel;
class QSplitter;
class QStackedWidget;
class QToolButton;

namespace spotty {

class GeneratorPanel;
class InterfaceBar;
class LoggingPanel;
class MacrosPanel;
class SearchPanel;
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

    /// \brief Обернуть содержимое в карточку с рамкой — общий вид всех блоков окна.
    QWidget *makeCard(QWidget *content);

    /// \brief Раздать текущие настройки всем потребителям.
    void applySettings();

    /// \brief Назначить действиям сочетания клавиш из настроек.
    void applyShortcuts();

    /// \brief Показать диалог настроек и применить результат.
    void showSettingsDialog();

    /// \brief Восстановить геометрию окна и положение разделителя.
    void restoreWindowState();

    /// \brief Записать состояние окна в настройки.
    void persistWindowState();

    /// \brief Пересобрать раскрашенные значки. Вызывается при смене темы.
    void updateIcons();

    /// \brief Открыть или закрыть канал в зависимости от текущего состояния.
    void toggleConnection();

    /// \brief Показать настройки интерфейса с заранее выбранным устройством.
    void showInterfaceSettings(const QString &interfaceId);

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

    void updateStatistics();
    void updateControlLines(const QVariantMap &lines);

    /**
     * \brief Показать файл лога в области терминала.
     *
     * Живой вывод при этом не теряется: он остаётся в своём буфере и возвращается на
     * экран кнопкой «к живому выводу». Показывать лог в том же буфере значило бы
     * затереть то, что пришло с устройства.
     */
    void showLogFile(const QString &filePath);

    /// \brief Вернуть в область терминала живой вывод сессии.
    void returnToLiveView();

    AppContext m_context;
    AppSettings m_settings;

    InterfaceBar *m_interfaceBar = nullptr;
    TerminalView *m_terminal = nullptr;
    SendBar *m_sendBar = nullptr;
    QSplitter *m_splitter = nullptr;
    QStackedWidget *m_panelStack = nullptr;
    QList<QToolButton *> m_panelButtons;
    QToolButton *m_settingsRailButton = nullptr; ///< Открывает диалог настроек; внизу рейки.

    GeneratorPanel *m_generatorPanel = nullptr;
    LoggingPanel *m_loggingPanel = nullptr;
    MacrosPanel *m_macrosPanel = nullptr;
    SearchPanel *m_searchPanel = nullptr;

    QToolButton *m_hexButton = nullptr;
    QToolButton *m_timestampButton = nullptr;
    QToolButton *m_directionButton = nullptr;
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
