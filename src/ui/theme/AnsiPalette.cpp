/**
 * \file AnsiPalette.cpp
 * \brief Реализация spotty::AnsiPalette.
 */
#include "AnsiPalette.h"

#include "ThemeManager.h"

namespace spotty {

namespace {

/**
 * \brief Базовые шестнадцать цветов ANSI.
 *
 * Значения близки к общепринятым в терминалах, но подобраны под монохромное оформление
 * Spotty: чуть приглушены, чтобы не выбиваться из серой гаммы окна.
 *
 * \param dark Тёмная тема — на светлом фоне те же оттенки пришлось бы затемнить.
 */
void fillBaseColors(QColor (&base)[16], bool dark)
{
    if (dark) {
        base[0]  = QColor(0x3b, 0x3f, 0x44); // чёрный
        base[1]  = QColor(0xd2, 0x6b, 0x6b); // красный
        base[2]  = QColor(0x8f, 0xb8, 0x6c); // зелёный
        base[3]  = QColor(0xd4, 0xb0, 0x6b); // жёлтый
        base[4]  = QColor(0x5f, 0x9d, 0xd9); // синий
        base[5]  = QColor(0xb2, 0x83, 0xc6); // пурпурный
        base[6]  = QColor(0x5f, 0xb3, 0xb3); // голубой
        base[7]  = QColor(0xc8, 0xcb, 0xcd); // белый
        base[8]  = QColor(0x60, 0x66, 0x6c);
        base[9]  = QColor(0xe8, 0x8b, 0x8b);
        base[10] = QColor(0xa8, 0xd0, 0x86);
        base[11] = QColor(0xe8, 0xc6, 0x88);
        base[12] = QColor(0x82, 0xb8, 0xea);
        base[13] = QColor(0xcb, 0xa0, 0xdc);
        base[14] = QColor(0x82, 0xcf, 0xcf);
        base[15] = QColor(0xf0, 0xf2, 0xf4);
    } else {
        base[0]  = QColor(0x2b, 0x2f, 0x33);
        base[1]  = QColor(0xb3, 0x33, 0x33);
        base[2]  = QColor(0x3d, 0x6b, 0x22);
        base[3]  = QColor(0x92, 0x6b, 0x00);
        base[4]  = QColor(0x1f, 0x6f, 0xb2);
        base[5]  = QColor(0x86, 0x45, 0x9c);
        base[6]  = QColor(0x27, 0x7d, 0x7d);
        base[7]  = QColor(0x6b, 0x70, 0x76);
        base[8]  = QColor(0x50, 0x55, 0x5b);
        base[9]  = QColor(0xd0, 0x3f, 0x3f);
        base[10] = QColor(0x4d, 0x86, 0x2b);
        base[11] = QColor(0xb3, 0x84, 0x00);
        base[12] = QColor(0x2a, 0x8a, 0xdb);
        base[13] = QColor(0xa1, 0x55, 0xbb);
        base[14] = QColor(0x30, 0x99, 0x99);
        base[15] = QColor(0x1d, 0x1f, 0x21);
    }
}

} // namespace

void AnsiPalette::setThemeColors(const ThemeColors &colors)
{
    // Тему различаем по светлоте фона, а не по перечислению: так палитра не зависит от
    // числа тем и заработает сама, если тем станет больше.
    fillBaseColors(m_base, colors.base.lightness() < 128);
    applyCustomColors();
}

void AnsiPalette::setCustomColors(const QStringList &colors)
{
    m_custom = colors.size() == 16 ? colors : QStringList{};
    applyCustomColors();
}

void AnsiPalette::applyCustomColors()
{
    if (m_custom.size() != 16)
        return;

    for (int i = 0; i < 16; ++i) {
        const QColor color(m_custom.at(i));
        // Неразобранное значение пропускаем, оставляя цвет темы: испорченная строка в
        // настройках не должна превращать текст в чёрный на чёрном.
        if (color.isValid())
            m_base[i] = color;
    }
}

QStringList AnsiPalette::baseColors() const
{
    QStringList result;
    result.reserve(16);
    for (const QColor &color : m_base)
        result.append(color.name(QColor::HexRgb));
    return result;
}

QColor AnsiPalette::indexedColor(quint8 index) const
{
    if (index < 16)
        return m_base[index];

    if (index < 232) {
        // Куб 6×6×6. Уровни неравномерны: первый шаг от 0 сразу до 95, дальше по 40 —
        // так задано в xterm, и отклонение сделало бы цвета непохожими на привычные.
        const int value = index - 16;
        static constexpr int levels[6] = {0, 95, 135, 175, 215, 255};
        return QColor(levels[(value / 36) % 6], levels[(value / 6) % 6], levels[value % 6]);
    }

    // 24 оттенка серого от почти чёрного до почти белого.
    const int grey = 8 + (index - 232) * 10;
    return QColor(grey, grey, grey);
}

QColor AnsiPalette::resolve(ColorSource source, quint8 index, quint32 rgb,
                            const QColor &fallback) const
{
    switch (source) {
    case ColorSource::Indexed:
        return indexedColor(index);
    case ColorSource::Rgb:
        return QColor(int((rgb >> 16) & 0xFF), int((rgb >> 8) & 0xFF), int(rgb & 0xFF));
    case ColorSource::Default:
        break;
    }
    return fallback;
}

QColor AnsiPalette::foreground(const TextStyle &style, const QColor &defaultColor) const
{
    if (style.inverse) {
        // При инверсии цветом текста становится фон. Если фон не задавался, берём фон
        // терминала — иначе инверсия была бы не видна.
        return resolve(style.backgroundSource, style.backgroundIndex, style.backgroundRgb,
                       defaultColor);
    }

    QColor color = resolve(style.foregroundSource, style.foregroundIndex,
                           style.foregroundRgb, defaultColor);

    // Тусклый режим приглушает цвет вместо смены начертания: отдельного «тонкого»
    // моноширинного шрифта в системе может не быть, а полупрозрачность работает всегда.
    if (style.faint)
        color.setAlpha(150);

    return color;
}

QColor AnsiPalette::background(const TextStyle &style, const QColor &defaultBackground) const
{
    if (style.inverse) {
        return resolve(style.foregroundSource, style.foregroundIndex, style.foregroundRgb,
                       defaultBackground);
    }

    if (style.backgroundSource == ColorSource::Default)
        return {}; // Закрашивать нечего — фон терминала уже нарисован.

    return resolve(style.backgroundSource, style.backgroundIndex, style.backgroundRgb,
                   defaultBackground);
}

} // namespace spotty
