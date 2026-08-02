/**
 * \file MainWindow.h
 * \brief Главное окно приложения.
 */
#pragma once

#include "AppContext.h"
#include "TerminalView.h"
#include "theme/ThemeManager.h"

#include <spotty/api/ChannelState.h>

#include <QMainWindow>

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
 * \brief Главное окно: постоянный каркас и восстановление своего состояния.
 *
 * \par Раскладка
 *
 * \verbatim
 *  ┌───────────────────────────────────────────────────────┐
 *  │ InterfaceBar: состояние · выбор интерфейса · настройки │
 *  ├───────────────────────────────────────────────────────┤
 *  │ панель терминала: текст/HEX · метки · очистка · слежение│
 *  ├──────────────────────────────────┬────────────────────┤
 *  │                                  │ ▌ рейка значков    │
 *  │        TerminalView              │ ▌ панель:          │
 *  │                                  │ ▌ макросы / логи / │
 *  │                                  │ ▌ поиск / генератор│
 *  ├──────────────────────────────────┴────────────────────┤
 *  │ SendBar: ввод · формат · терминация · Отправить       │
 *  ├───────────────────────────────────────────────────────┤
 *  │ строка состояния: счётчики, скорость, линии управления │
 *  └───────────────────────────────────────────────────────┘
 * \endverbatim
 *
 * Панели справа пока заглушки — их наполняет этап 3.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const AppContext &context, QWidget *parent = nullptr);

protected:
    /// \brief Сохраняет состояние окна перед закрытием.
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildMenus();

    /// \brief Панель управления показом терминала.
    QWidget *buildTerminalToolbar();

    /// \brief Правая панель: рейка значков и стопка страниц.
    QWidget *buildSidePanel();

    void restoreState();
    void persistState();

    void setTheme(ThemeManager::Theme theme);

    /// \brief Пересобрать раскрашенные значки. Вызывается при смене темы.
    void updateIcons();

    /// \brief Открыть или закрыть канал в зависимости от текущего состояния.
    void toggleConnection();

    /// \brief Показать диалог настроек интерфейса и применить результат.
    void showInterfaceSettings(const QString &interfaceId);

    /// \brief Отразить новое состояние канала во всех элементах окна.
    void applyChannelState(ChannelState state, const QString &detail);

    /// \brief Обновить счётчики в строке состояния.
    void updateStatistics();

    /// \brief Обновить индикаторы линий управления.
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

    GeneratorPanel *m_generatorPanel = nullptr;
    LoggingPanel *m_loggingPanel = nullptr;
    MacrosPanel *m_macrosPanel = nullptr;
    SearchPanel *m_searchPanel = nullptr;

    /// \brief Буфер под просматриваемый файл лога; живой вывод хранится отдельно.
    TerminalBuffer *m_logBuffer = nullptr;

    /// \brief Полоса с именем открытого лога и кнопкой возврата.
    QWidget *m_logViewBar = nullptr;
    QLabel *m_logViewLabel = nullptr;

    AppContext m_context;

    InterfaceBar *m_interfaceBar = nullptr;
    TerminalView *m_terminal = nullptr;
    SendBar *m_sendBar = nullptr;
    QSplitter *m_splitter = nullptr;
    QStackedWidget *m_panelStack = nullptr;
    QList<QToolButton *> m_panelButtons;

    QToolButton *m_hexButton = nullptr;
    QToolButton *m_timestampButton = nullptr;
    QToolButton *m_directionButton = nullptr;
    QToolButton *m_clearButton = nullptr;
    QToolButton *m_followButton = nullptr;

    QLabel *m_statsLabel = nullptr;
    QLabel *m_linesLabel = nullptr;
};

} // namespace spotty
