/**
 * \file AnsiPalette.h
 * \brief Преобразование цветов ANSI в цвета текущей темы.
 */
#pragma once

#include <terminal/TextStyle.h>

#include <QColor>
#include <QStringList>

namespace spotty {

struct ThemeColors;

/**
 * \class AnsiPalette
 * \brief Превращает номер цвета ANSI в конкретный QColor.
 *
 * \par Разделение обязанностей
 *
 * Разборщик ANSI живёт в ядре и цветов не знает — он хранит номер (см. spotty::TextStyle).
 * Здесь номер превращается в цвет, и происходит это при каждой отрисовке. Благодаря этому
 * смена темы перекрашивает уже накопленный вывод: если бы цвет вычислялся один раз при
 * разборе, старые строки остались бы в цветах прежней темы.
 *
 * \par Устройство палитры xterm-256
 *
 * - 0–7 — основные цвета, 8–15 — их яркие варианты. Задаются темой, потому что «красный»
 *   на тёмном и светлом фоне — разные цвета.
 * - 16–231 — куб 6×6×6, вычисляется по формуле.
 * - 232–255 — 24 оттенка серого, тоже по формуле.
 */
class AnsiPalette
{
public:
    /**
     * \brief Задать базовые шестнадцать цветов из темы.
     *
     * Вызывается при смене темы. Остальные цвета палитры вычисляются и от темы не зависят.
     *
     * \note Если заданы пользовательские цвета, они имеют приоритет и тему переопределяют.
     */
    void setThemeColors(const ThemeColors &colors);

    /**
     * \brief Задать свои базовые цвета вместо цветов темы.
     * \param colors Ровно шестнадцать значений вида `"#rrggbb"`; любое другое число
     *        записей возвращает палитру к цветам темы.
     *
     * Пустой список — не «чёрная палитра», а «вернуть как было»: пока пользователь не
     * задал свои цвета, смена темы должна продолжать менять и палитру.
     */
    void setCustomColors(const QStringList &colors);

    /// \brief Текущие шестнадцать базовых цветов в записи `"#rrggbb"`.
    QStringList baseColors() const;

    /**
     * \brief Цвет по номеру палитры xterm-256.
     * \param index 0–255.
     */
    QColor indexedColor(quint8 index) const;

    /**
     * \brief Цвет текста для отрезка.
     * \param style Оформление отрезка.
     * \param defaultColor Цвет, если в оформлении цвет не задан.
     *
     * Учитывает инверсию: при SGR 7 цвета текста и фона меняются местами.
     */
    QColor foreground(const TextStyle &style, const QColor &defaultColor) const;

    /**
     * \brief Цвет фона для отрезка.
     * \return Недействительный QColor, если фон не задан — тогда его не нужно закрашивать.
     */
    QColor background(const TextStyle &style, const QColor &defaultBackground) const;

private:
    /// \brief Цвет без учёта инверсии.
    QColor resolve(ColorSource source, quint8 index, quint32 rgb,
                   const QColor &fallback) const;

    /// \brief Применить пользовательские цвета поверх текущих, если они заданы.
    void applyCustomColors();

    QColor m_base[16];

    /// \brief Цвета, заданные пользователем; пустой список — используются цвета темы.
    QStringList m_custom;
};

} // namespace spotty
