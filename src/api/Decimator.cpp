/**
 * \file Decimator.cpp
 * \brief Реализация spotty::Decimator.
 */
#include <spotty/data/Decimator.h>

#include <QtGlobal>
#include <QtMath>

#include <algorithm>
#include <limits>

namespace spotty {

std::optional<qint64> Decimator::coordinateAt(const SampleBuffer &buffer, int row,
                                               int coordinateColumn)
{
    if (row < 0 || row >= buffer.sampleCount())
        return std::nullopt;
    if (coordinateColumn < 0)
        return buffer.timestamp(row);
    if (coordinateColumn >= buffer.columnCount())
        return std::nullopt;

    const double value = buffer.at(row, coordinateColumn);
    const double scaled = value * double(kCounterCoordinateScale);
    // Вблизи предела qint64 шаг double уже больше единицы, поэтому оставляем запас в
    // несколько ULP до преобразования. Иначе qRound64 мог бы переполниться ещё до того,
    // как успеет вернуть результат.
    constexpr double kSafeLimit = double(std::numeric_limits<qint64>::max()) - 4'096.0;
    if (!qIsFinite(scaled) || qAbs(scaled) >= kSafeLimit)
        return std::nullopt;
    return qRound64(scaled);
}

int Decimator::lowerBound(const SampleBuffer &buffer, qint64 coordinate, int coordinateColumn)
{
    int low = 0;
    int high = buffer.sampleCount();

    while (low < high) {
        const int middle = low + (high - low) / 2;
        const std::optional<qint64> value = coordinateAt(buffer, middle, coordinateColumn);
        if (value && *value < coordinate)
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

qint64 Decimator::maximumConnectedGap(const SampleBuffer &buffer,
                                      const XTransform &transform, int pixelColumns,
                                      int coordinateColumn)
{
    const qint64 span = qMax<qint64>(1, transform.to - transform.from);
    const qint64 visualLimit = qMax<qint64>(1, span / qMax(1, pixelColumns) * 3);
    if (buffer.sampleCount() < 2)
        return visualLimit;

    QList<qint64> intervals;
    intervals.reserve(buffer.sampleCount() - 1);
    for (int row = 1; row < buffer.sampleCount(); ++row) {
        const std::optional<qint64> previous = coordinateAt(buffer, row - 1, coordinateColumn);
        const std::optional<qint64> current = coordinateAt(buffer, row, coordinateColumn);
        const qint64 interval = current && previous ? *current - *previous : 0;
        if (interval > 0)
            intervals.append(interval);
    }
    if (intervals.isEmpty())
        return visualLimit;

    auto median = intervals.begin() + intervals.size() / 2;
    std::nth_element(intervals.begin(), median, intervals.end());
    const qint64 typical = *median;
    const qint64 normalLimit = typical > std::numeric_limits<qint64>::max() / 3
                                    ? std::numeric_limits<qint64>::max()
                                    : typical * 3;
    return qMax(visualLimit, normalLimit);
}

Decimator::Result Decimator::reduce(const SampleBuffer &buffer, int column,
                                    const XTransform &transform, int pixelColumns,
                                    Accumulator accumulator, qint64 maximumConnectedGap,
                                    int coordinateColumn)
{
    Result result;
    if (pixelColumns <= 0 || column < 0 || column >= buffer.columnCount())
        return result;

    const int firstVisible = lowerBound(buffer, transform.from, coordinateColumn);
    if (maximumConnectedGap <= 0)
        maximumConnectedGap =
            Decimator::maximumConnectedGap(buffer, transform, pixelColumns, coordinateColumn);

    // Накопление считается от начала буфера, а не от левого края окна: «сколько набежало
    // всего» — это ответ про весь сеанс, и прокрутка не должна его менять.
    double running = 0.0;
    int firstRow = 0;
    if (accumulator == Accumulator::RunningSum) {
        firstRow = 0;
    } else {
        // Нужен один сосед слева, чтобы линия пришла к рамке окна, а не начиналась после
        // неё. Более старые точки на форму видимого фрагмента уже не влияют.
        firstRow = qMax(0, firstVisible - 1);
    }

    const int total = buffer.sampleCount();

    int currentColumn = -1;
    double lowest = 0.0;
    double highest = 0.0;
    bool columnHasData = false;
    bool runOpen = false;
    bool hasPreviousPoint = false;
    qint64 previousCoordinate = 0;

    const auto flushColumn = [&] {
        if (!columnHasData)
            return;
        if (!runOpen) {
            result.runStarts.append(int(result.columns.size()));
            runOpen = true;
        }
        result.columns.append(Column{currentColumn, lowest, highest});
        if (currentColumn >= 0 && currentColumn < pixelColumns)
            result.visible = PlotScales::merge(result.visible, {lowest, highest, true});
        columnHasData = false;
    };

    for (int row = firstRow; row < total; ++row) {
        const std::optional<qint64> coordinate = coordinateAt(buffer, row, coordinateColumn);

        double value = buffer.at(row, column);

        if (accumulator == Accumulator::RunningSum) {
            // Пропуск не обнуляет сумму и не прерывает её: он означает «в этой строке поля
            // не было», а накопленное до него никуда не делось.
            if (qIsFinite(value))
                running += value;
            value = running;
            if (!coordinate || (*coordinate < transform.from && row != firstVisible - 1))
                continue;
        }

        const bool insideWindow = coordinate && *coordinate >= transform.from
                                  && *coordinate <= transform.to;
        const bool leftNeighbour = coordinate && row == firstVisible - 1
                                   && *coordinate < transform.from;
        const bool rightNeighbour = coordinate && *coordinate > transform.to;
        if (!insideWindow && !leftNeighbour && !rightNeighbour)
            continue;

        // Соседние точки лежат ровно за границей. Их координаты не описывают временную
        // шкалу, а лишь дают растеризатору отрезок, который пересечёт рамку после clipRect.
        // Пределы автомасштаба выше учитывают только настоящую видимую часть.
        const int pixel = insideWindow ? transform.columnOf(*coordinate, pixelColumns)
                                       : (leftNeighbour ? -1 : pixelColumns);

        if (pixel != currentColumn) {
            flushColumn();
            currentColumn = pixel;
        }

        if (!coordinate || !qIsFinite(value)) {
            // Пропуск закрывает отрезок: следующая колонка начнёт новый.
            flushColumn();
            runOpen = false;
            hasPreviousPoint = false;
            if (rightNeighbour)
                break;
            continue;
        }

        if (hasPreviousPoint && *coordinate - previousCoordinate > maximumConnectedGap) {
            // Пауза в потоке — отсутствие измерений, а не неизвестная прямая между двумя
            // значениями. Новый отсчёт начинает свой отдельный участок.
            flushColumn();
            runOpen = false;
        }

        if (!columnHasData) {
            lowest = value;
            highest = value;
            columnHasData = true;
        } else {
            lowest = qMin(lowest, value);
            highest = qMax(highest, value);
        }
        previousCoordinate = *coordinate;
        hasPreviousPoint = true;

        // После первого соседа справа дальше читать бессмысленно: он уже замыкает линию
        // у края, а следующий всё равно останется за пределами окна.
        if (rightNeighbour)
            break;
    }

    flushColumn();
    // Одних соседей недостаточно: через пустое окно нельзя достоверно провести кривую, а
    // шкала для такого фрагмента не определена. Контекст служит только продолжением ряда,
    // у которого действительно есть данные внутри окна.
    if (!result.visible.valid) {
        result.columns.clear();
        result.runStarts.clear();
    }
    return result;
}

} // namespace spotty
