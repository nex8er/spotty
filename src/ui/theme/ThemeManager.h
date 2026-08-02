/**
 * \file ThemeManager.h
 * \brief Оформление приложения: палитра, стиль, таблица стилей.
 */
#pragma once

#include <QColor>
#include <QObject>
#include <QString>

namespace spotty {

/**
 * \struct ThemeColors
 * \brief Полный набор цветов, которыми пользуется интерфейс.
 *
 * \par Зачем отдельная структура
 *
 * Область терминала рисует себя сама, а не средствами стилизации виджетов, поэтому ей
 * нужны те же цвета, что использует таблица стилей. Единственная структура — то, что не
 * даёт этим двум наборам разойтись: цвет объявляется здесь один раз и подставляется в QSS
 * по имени.
 *
 * \see spotty::ThemeManager::colors()
 */
struct ThemeColors
{
    QColor window;    ///< Фон приложения.
    QColor panel;     ///< Боковые панели, полосы инструментов, заголовки.
    QColor base;      ///< Фон терминала и полей ввода.
    QColor border;    ///< Разделительные линии толщиной в пиксель.
    QColor text;      ///< Основной текст.
    QColor textMuted; ///< Второстепенные подписи, метки времени, недоступные пункты.
    QColor accent;    ///< Единственный нейтральный цвет; применяется скупо.
    QColor accentText;///< Текст поверх акцентного фона.
    QColor selection; ///< Фон выделения.

    QColor rxText;    ///< Принятые данные.
    QColor txText;    ///< Отправленные данные, отражённые в терминале.
    QColor errorText; ///< Ошибки.
    QColor okText;    ///< Успешные состояния, например открытый порт.
};

/**
 * \class ThemeManager
 * \brief Применяет оформление Spotty: стиль Fusion, собственная палитра и таблица стилей.
 *
 * \par Почему Fusion, а не родной стиль системы
 *
 * Выбран единый вид на всех операционных системах. Родной стиль дал бы «свой» облик на
 * каждой платформе, но тёмная тема ведёт себя всюду по-разному, и область терминала
 * осталась бы единственной постоянной частью в остальном плавающего окна. Скриншоты и
 * документация тоже перестали бы быть универсальными.
 *
 * \par Подстановка цветов в QSS
 *
 * Файл `resources/themes/spotty.qss` содержит плейсхолдеры вида `@panel`, `@textMuted`.
 * Подстановка идёт от длинных имён к коротким — иначе `@text` затёр бы начало
 * `@textMuted`, превратив его в `#d8dadcMuted`, и правило молча отбросил бы разборщик
 * CSS. Оставшиеся после подстановки `@имя` вызывают предупреждение: опечатка в стилях не
 * должна тихо ничего не делать.
 */
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    /// \brief Доступные темы.
    enum class Theme {
        Dark,  ///< Тёмная.
        Light, ///< Светлая.
    };
    Q_ENUM(Theme)

    explicit ThemeManager(QObject *parent = nullptr);

    /**
     * \brief Экземпляр на процесс.
     * \return Первый созданный ThemeManager или `nullptr`, если он ещё не создан.
     *
     * Нужен там, где протащить AppContext неудобно, — прежде всего в spotty::MdiIcons,
     * который вызывается из статических функций.
     */
    static ThemeManager *instance();

    /// \return Текущая тема.
    Theme theme() const { return m_theme; }

    /// \return Цвета текущей темы.
    const ThemeColors &colors() const { return m_colors; }

    /**
     * \brief Переключить тему и применить её немедленно.
     * \param theme Новая тема.
     *
     * Устанавливает стиль, палитру и таблицу стилей приложения, после чего испускает
     * themeChanged().
     */
    void setTheme(Theme theme);

    /// \brief Имя темы для записи в настройки: `"dark"` или `"light"`.
    static QString themeToString(Theme theme);

    /**
     * \brief Разобрать имя темы из настроек.
     * \param name Строка из конфигурации; регистр не важен.
     * \param fallback Что вернуть при неизвестном имени.
     */
    static Theme themeFromString(const QString &name, Theme fallback = Theme::Dark);

Q_SIGNALS:
    /**
     * \brief Тема применена.
     * \param theme Новая тема.
     *
     * Испускается уже после установки палитры и таблицы стилей. Здесь виджеты
     * пересоздают закэшированные цвета и раскрашенные иконки: значки Material Design
     * запекаются в пиксельные карты с конкретным цветом, и после смены темы их нужно
     * построить заново.
     *
     * \see spotty::MdiIcons
     */
    void themeChanged(Theme theme);

private:
    /// \brief Собрать палитру и таблицу стилей и установить их приложению.
    void apply();

    Theme m_theme = Theme::Dark; ///< Текущая тема.
    ThemeColors m_colors;        ///< Цвета текущей темы.
};

} // namespace spotty
