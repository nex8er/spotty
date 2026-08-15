/**
 * \file Decimator.cpp
 * \brief Реализация spotty::Decimator.
 */
#include <spotty/data/Decimator.h>

#include <QtGlobal>
#include <QtMath>

namespace spotty {

int Decimator::lowerBound(const SampleBuffer &buffer, qint64 time)
{
    int low = 0;
    int high = buffer.sampleCount();

    while (low < high) {
        const int middle = low + (high - low) / 2;
        if (buffer.timestamp(middle) < time)
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

Decimator::Result Decimator::reduce(const SampleBuffer &buffer, int column,
                                    const XTransform &transform, int pixelColumns,
                                    Accumulator accumulator)
{
    Result result;
    if (pixelColumns <= 0 || column < 0 || column >= buffer.columnCount())
        return result;

    // Накопление считается от начала буфера, а не от левого края окна: «сколько набежало
    // всего» — это ответ про весь сеанс, и прокрутка не должна его менять.
    double running = 0.0;
    int firstRow = 0;
    if (accumulator == Accumulator::RunningSum) {
        firstRow = 0;
    } else {
        firstRow = lowerBound(buffer, transform.from);
    }

    const int total = buffer.sampleCount();

    int currentColumn = -1;
    double lowest = 0.0;
    double highest = 0.0;
    bool columnHasData = false;
    bool runOpen = false;

    const auto flushColumn = [&] {
        if (!columnHasData)
            return;
        if (!runOpen) {
            result.runStarts.append(int(result.columns.size()));
            runOpen = true;
        }
        result.columns.append(Column{currentColumn, lowest, highest});
        result.visible = PlotScales::merge(result.visible, {lowest, highest, true});
        columnHasData = false;
    };

    for (int row = firstRow; row < total; ++row) {
        const qint64 stamp = buffer.timestamp(row);
        if (stamp > transform.to)
            break;

        double value = buffer.at(row, column);

        if (accumulator == Accumulator::RunningSum) {
            // Пропуск не обнуляет сумму и не прерывает её: он означает «в этой строке поля
            // не было», а накопленное до него никуда не делось.
            if (qIsFinite(value))
                running += value;
            value = running;
            if (stamp < transform.from)
                continue;
        }

        const int pixel = transform.columnOf(stamp, pixelColumns);
        if (pixel < 0)
            continue;

        if (pixel != currentColumn) {
            flushColumn();
            currentColumn = pixel;
        }

        if (!qIsFinite(value)) {
            // Пропуск закрывает отрезок: следующая колонка начнёт новый.
            flushColumn();
            runOpen = false;
            continue;
        }

        if (!columnHasData) {
            lowest = value;
            highest = value;
            columnHasData = true;
        } else {
            lowest = qMin(lowest, value);
            highest = qMax(highest, value);
        }
    }

    flushColumn();
    return result;
}

} // namespace spotty
