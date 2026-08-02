/**
 * \file MainWindow.h
 * \brief Главное окно приложения.
 */
#pragma once

#include "AppContext.h"
#include "theme/ThemeManager.h"

#include <QMainWindow>

class QPlainTextEdit;
class QSplitter;
class QStackedWidget;
class QToolButton;

namespace spotty {

class InterfaceBar;

/**
 * \class MainWindow
 * \brief Главное окно: постоянный каркас и восстановление своего состояния.
 *
 * \par Раскладка
 *
 * \verbatim
 *  ┌───────────────────────────────────────────────────────┐
 *  │ InterfaceBar: состояние · выбор интерфейса · настройки │
 *  ├──────────────────────────────────┬────────────────────┤
 *  │                                  │ ▌ рейка значков    │
 *  │        область терминала         │ ▌ панель:          │
 *  │                                  │ ▌ макросы / логи / │
 *  │                                  │ ▌ поиск / генератор│
 *  ├──────────────────────────────────┴────────────────────┤
 *  │ строка отправки: ввод · формат · терминация · Отправить│
 *  └───────────────────────────────────────────────────────┘
 * \endverbatim
 *
 * \par Состояние этапа 1
 *
 * Терминал, строка отправки и панели пока заглушки. Центральная область работает как
 * диагностический отчёт о запуске, строка отправки выключена, панели показывают
 * заголовок. Каркас вокруг них — то, что наполняют следующие этапы.
 *
 * \par Что запоминается
 *
 * Геометрия окна, положение разделителя, активная панель, тема и выбранный интерфейс.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param context Службы приложения; копируется, указатели внутри переживают окно.
     * \param parent Родительский виджет.
     */
    explicit MainWindow(const AppContext &context, QWidget *parent = nullptr);

protected:
    /// \brief Сохраняет состояние окна перед закрытием.
    void closeEvent(QCloseEvent *event) override;

private:
    /// \brief Собрать центральный виджет и раскладку.
    void buildUi();

    /// \brief Собрать строку меню.
    void buildMenus();

    /// \brief Собрать правую панель: рейку значков и стопку страниц.
    QWidget *buildSidePanel();

    /// \brief Собрать нижнюю строку отправки.
    QWidget *buildSendBar();

    /// \brief Восстановить сохранённое состояние окна.
    void restoreState();

    /// \brief Записать состояние окна в настройки.
    void persistState();

    /// \brief Переключить тему и запомнить выбор.
    void setTheme(ThemeManager::Theme theme);

    /// \brief Пересобрать раскрашенные значки. Вызывается при смене темы.
    void updateIcons();

    /**
     * \brief Заполнить центральную область отчётом о запуске.
     *
     * Пока нет терминала, центральная область служит диагностикой. Обнаружение плагинов —
     * то, что чаще всего идёт не так на новой машине, поэтому отчёт показывается целиком,
     * а не прячется в журнал.
     */
    void showStartupReport();

    AppContext m_context;

    InterfaceBar *m_interfaceBar = nullptr;
    QPlainTextEdit *m_terminalPlaceholder = nullptr; ///< Заглушка терминала (этап 2).
    QSplitter *m_splitter = nullptr;      ///< Разделитель «терминал / панели».
    QStackedWidget *m_panelStack = nullptr; ///< Страницы правых панелей.
    QList<QToolButton *> m_panelButtons;  ///< Кнопки рейки, по одной на панель.
};

} // namespace spotty
