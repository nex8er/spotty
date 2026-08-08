/**
 * \file TextStyle.h
 * \brief Атрибуты оформления одного отрезка текста.
 */
#pragma once

#include <QtCore/qtypes.h>

namespace spotty {

/**
 * \enum ColorSource
 * \brief Каким способом задан цвет в последовательности SGR.
 */
enum class ColorSource : quint8 {
    Default, ///< Цвет не задавался — берётся из темы.
    Indexed, ///< Номер в палитре xterm-256.
    Rgb,     ///< Точный цвет из последовательности 38;2;r;g;b.
};

/**
 * \struct TextStyle
 * \brief Оформление отрезка текста: цвета и начертание.
 *
 * \par Почему цвет хранится номером, а не QColor
 *
 * Разбор ANSI живёт в `spotty-core`, который ничего не знает про темы. Первые
 * шестнадцать цветов палитры ANSI задаются темой оформления — «красный» в тёмной и
 * светлой теме это разные значения. Если бы разборщик сразу превращал номер в QColor, он
 * либо потянул бы за собой тему в ядро, либо зафиксировал бы цвета намертво, и смена
 * темы не перекрашивала бы уже накопленный вывод.
 *
 * Поэтому ядро хранит номер, а превращает его в цвет виджет — при каждой отрисовке.
 *
 * \see spotty::AnsiPalette — преобразование номера в цвет темы.
 */
struct TextStyle
{
    ColorSource foregroundSource = ColorSource::Default;
    ColorSource backgroundSource = ColorSource::Default;

    quint8 foregroundIndex = 0; ///< Номер в палитре xterm-256 при ColorSource::Indexed.
    quint8 backgroundIndex = 0;

    /**
     * \brief Точный цвет при ColorSource::Rgb, упакованный как `0x00RRGGBB`.
     *
     * Именно quint32, а не QRgb: QRgb объявлен в QtGui, а `spotty-core` намеренно не
     * зависит ни от QtGui, ни от QtWidgets. Распаковкой занимается виджет.
     */
    quint32 foregroundRgb = 0;
    quint32 backgroundRgb = 0;

    bool bold = false;
    bool faint = false;
    bool italic = false;
    bool underline = false;

    /// \brief Поменять местами цвета текста и фона (SGR 7).
    bool inverse = false;

    bool strikeOut = false;

    bool operator==(const TextStyle &) const = default;
};

} // namespace spotty
