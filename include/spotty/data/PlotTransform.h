/**
 * \file PlotTransform.h
 * \brief Перевод данных плоттера в координаты поля и обратно.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>
#include <spotty/data/PlotModel.h>

namespace spotty {

/**
 * \struct XTransform
 * \brief Отображение времени на горизонталь поля.
 *
 * Ось X — реальное время, а не номер отсчёта. При неравномерном потоке — пауза в секунду,
 * потом пачка строк — равноотстоящая ось показывала бы паузу обычным шагом, то есть
 * искажала бы форму сигнала ровно там, где на неё и смотрят.
 */
struct SPOTTY_API_EXPORT XTransform
{
    qint64 from = 0; ///< Время левого края поля, нс.
    qint64 to = 1;   ///< Время правого края поля, нс.
    double left = 0.0;
    double width = 1.0;

    double xOf(qint64 time) const;
    qint64 timeAt(double x) const;

    /// \brief Номер пиксельной колонки для времени; -1, если время вне окна.
    int columnOf(qint64 time, int columns) const;
};

/**
 * \struct YScale
 * \brief Отображение значения на вертикаль поля.
 *
 * У каждого ряда она своя: ряды меряют разные величины, и одна шкала на всех прижимает
 * милливольты к нулю рядом с оборотами в минуту.
 */
struct SPOTTY_API_EXPORT YScale
{
    double minimum = 0.0;
    double maximum = 1.0;
    double top = 0.0;
    double height = 1.0;

    double yOf(double value) const;
    double valueAt(double y) const;
};

/**
 * \class PlotScales
 * \brief Правило, по которому ряд получает свои пределы.
 */
class SPOTTY_API_EXPORT PlotScales
{
public:
    /**
     * \struct Range
     * \brief Пределы вместе с признаком «данных не было».
     *
     * Признак отдельный, а не «минимум равен максимуму»: ноль — законное значение, и
     * пустую колонку от колонки из одних нулей иначе не отличить.
     */
    struct Range
    {
        double minimum = 0.0;
        double maximum = 0.0;
        bool valid = false;
    };

    /**
     * \brief Пределы шкалы ряда по трём источникам, строго в этом порядке.
     * \param series Оформление ряда: в нём живут пользовательские пределы.
     * \param own Видимые пределы самого ряда; отдаёт Decimator тем же проходом.
     * \param group Объединённые пределы выделенной группы; недействительны, если ряд не в ней.
     *
     * 1. Заданные пользователем пределы — их не перебивает ничто: он ввёл эти числа руками.
     * 2. Общая шкала выделенной группы — ради неё группу и выделяют, чтобы сравнить ряды.
     * 3. Иначе автомасштаб по своим видимым значениям.
     *
     * Результат уже с запасом по краям: кривая, прижатая к самому краю поля, наполовину
     * срезается рамкой. Постоянный сигнал получает ±1, иначе высота шкалы была бы нулевой
     * и деление на неё дало бы бесконечность.
     */
    static Range resolve(const PlotSeries &series, const Range &own, const Range &group);

    /// \brief Добавить запас по краям и развести совпавшие пределы.
    static Range padded(const Range &range);

    /// \brief Объединение двух диапазонов; недействительные пропускаются.
    static Range merge(const Range &first, const Range &second);
};

} // namespace spotty
