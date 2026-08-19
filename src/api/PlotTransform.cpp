/**
 * \file PlotTransform.cpp
 * \brief Реализация преобразований координат плоттера.
 */
#include <spotty/data/PlotTransform.h>

#include <QtGlobal>
#include <QtMath>

namespace spotty {

namespace {

/// \brief Запас над и под кривой, долей высоты шкалы.
constexpr double kPadding = 0.05;

} // namespace

double XTransform::xOf(qint64 time) const
{
    const qint64 span = to - from;
    if (span <= 0)
        return left;
    return left + double(time - from) / double(span) * width;
}

qint64 XTransform::timeAt(double x) const
{
    if (width <= 0.0)
        return from;
    return from + qint64((x - left) / width * double(to - from));
}

int XTransform::columnOf(qint64 time, int columns) const
{
    if (columns <= 0 || time < from || time > to)
        return -1;

    const qint64 span = to - from;
    if (span <= 0)
        return 0;

    // Правый край попадает ровно в columns, а колонок всего columns — зажимаем, иначе
    // последний отсчёт окна выпадал бы из результата.
    return int(qBound(qint64(0), qint64(double(time - from) / double(span) * columns),
                      qint64(columns - 1)));
}

double YScale::yOf(double value) const
{
    const double span = maximum - minimum;
    if (span <= 0.0)
        return top + height;
    return top + height - (value - minimum) / span * height;
}

double YScale::valueAt(double y) const
{
    if (height <= 0.0)
        return minimum;
    return minimum + (top + height - y) / height * (maximum - minimum);
}

PlotScales::Range PlotScales::padded(const Range &range)
{
    if (!range.valid)
        return {0.0, 1.0, false};

    Range result = range;

    if (qFuzzyCompare(result.minimum, result.maximum)) {
        // Постоянный сигнал: без запаса высота шкалы нулевая, а линия легла бы точно на
        // край и стала невидимой.
        result.minimum -= 1.0;
        result.maximum += 1.0;
        return result;
    }

    const double padding = (result.maximum - result.minimum) * kPadding;
    result.minimum -= padding;
    result.maximum += padding;
    return result;
}

PlotScales::Range PlotScales::merge(const Range &first, const Range &second)
{
    if (!first.valid)
        return second;
    if (!second.valid)
        return first;

    return {qMin(first.minimum, second.minimum), qMax(first.maximum, second.maximum), true};
}

PlotScales::Range PlotScales::resolve(const PlotSeries &series, const Range &own,
                                      const Range &group)
{
    // Пользовательские пределы идут первыми и не дополняются запасом: человек ввёл эти
    // числа, чтобы видеть именно их на краях шкалы.
    if (series.hasCustomRange)
        return {series.customMinimum, series.customMaximum, true};

    if (group.valid)
        return padded(group);

    if (own.valid)
        return padded(own);

    return {0.0, 1.0, false};
}

} // namespace spotty
