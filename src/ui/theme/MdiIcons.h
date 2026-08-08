/**
 * \file MdiIcons.h
 * \brief Иконки Material Design Icons из встроенного веб-шрифта.
 */
#pragma once

#include <spotty/ui/MdiCodepoints.h>

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

namespace spotty {

/**
 * \class MdiIcons
 * \brief Отрисовка значков Material Design Icons из встроенного шрифта.
 *
 * \par Почему цвет запекается, а не берётся при отрисовке
 *
 * Значок собирается в пиксельную карту с явно заданным цветом. Прозрачнее было бы
 * определять цвет в момент рисования, но QIcon кэширует пиксельные карты, и после смены
 * темы виджет получил бы из кэша значок прежнего цвета — на тёмном фоне тёмный.
 *
 * Поэтому виджеты пересобирают свои иконки по сигналу
 * spotty::ThemeManager::themeChanged(). У каждого такого класса есть метод
 * `updateIcons()`, вызываемый и из конструктора, и из обработчика смены темы.
 *
 * \par Набор значков
 *
 * Шрифт встроен целиком (около 7000 глифов, ~1,3 МБ), но именованные константы
 * порождаются только для используемых. Список ведётся в `tools/gen_mdi.py`, результат —
 * `MdiCodepoints.h`.
 *
 * \code
 * button->setIcon(MdiIcons::icon(mdi::Magnify, 18));
 * \endcode
 */
class MdiIcons
{
public:
    /**
     * \brief Загрузить веб-шрифт в базу шрифтов приложения.
     * \return `false`, если ресурс отсутствует.
     *
     * Вызывается один раз при запуске. Отсутствие шрифта не фатально: методы вернут пустые
     * QIcon, кнопки останутся без значков, программа продолжит работать.
     */
    static bool initialize();

    /// \return `true`, если шрифт загружен и значки доступны.
    static bool isAvailable();

    /**
     * \brief Шрифт значков нужного размера — для прямой отрисовки глифа.
     * \param pixelSize Размер в пикселях.
     */
    static QFont font(int pixelSize);

    /**
     * \brief Один глиф как строка — для QLabel или собственной отрисовки.
     * \param codepoint Константа из пространства имён spotty::mdi.
     */
    static QString glyph(char32_t codepoint);

    /**
     * \brief Значок заданного цвета.
     * \param codepoint Константа из пространства имён spotty::mdi.
     * \param color Цвет отрисовки.
     * \param size Сторона квадрата в логических пикселях.
     * \return Готовый QIcon; результат кэшируется, повторные вызовы дёшевы.
     *
     * Отрисовка идёт в физическом разрешении экрана, поэтому значок остаётся чётким на
     * дисплеях с высокой плотностью точек.
     */
    static QIcon icon(char32_t codepoint, const QColor &color, int size = 32);

    /// \brief Значок цветом обычного текста текущей темы.
    static QIcon icon(char32_t codepoint, int size = 32);

    /// \brief Значок приглушённым цветом — для второстепенных действий.
    static QIcon mutedIcon(char32_t codepoint, int size = 32);
};

} // namespace spotty
