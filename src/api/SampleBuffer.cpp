/**
 * \file SampleBuffer.cpp
 * \brief Реализация spotty::SampleBuffer.
 */
#include <spotty/data/SampleBuffer.h>

#include <QtGlobal>
#include <QtMath>

#include <algorithm>

namespace spotty {

namespace {

/// \brief Наименьшая осмысленная ёмкость: по одной точке на концах отрезка.
constexpr int kMinimumCapacity = 2;

} // namespace

SampleBuffer::SampleBuffer()
{
    m_stamps.resize(m_capacity);
}

int SampleBuffer::physicalRow(int row) const
{
    return (m_head + row) % m_capacity;
}

void SampleBuffer::setCapacity(int samples)
{
    const int wanted = qMax(kMinimumCapacity, samples);
    if (wanted == m_capacity)
        return;

    // Ужимаясь, оставляем хвост: свежие данные нужнее старых, и именно их видит пользователь.
    relayout(wanted, m_stride, qMin(m_size, wanted));
}

void SampleBuffer::setColumnLimit(int columns)
{
    m_columnLimit = qMax(1, columns);
}

qint64 SampleBuffer::timestamp(int row) const
{
    if (row < 0 || row >= m_size)
        return 0;
    return m_stamps.at(physicalRow(row));
}

double SampleBuffer::at(int row, int column) const
{
    if (row < 0 || row >= m_size || column < 0 || column >= m_stride)
        return qQNaN();
    return m_cells.at(qsizetype(physicalRow(row)) * m_stride + column);
}

void SampleBuffer::copyColumn(int column, int fromRow, int count, double *out) const
{
    if (!out)
        return;
    for (int i = 0; i < count; ++i)
        out[i] = at(fromRow + i, column);
}

void SampleBuffer::growStride(int columns)
{
    relayout(m_capacity, columns, m_size);
}

void SampleBuffer::relayout(int newCapacity, int newStride, int keep)
{
    QList<double> cells(qsizetype(newCapacity) * newStride, qQNaN());
    QList<qint64> stamps(newCapacity, 0);

    // Хранимые строки переписываются в логическом порядке, поэтому голова кольца уезжает
    // в ноль, а новые колонки остаются NaN — добивать их отдельно не нужно.
    const int firstKept = m_size - keep;
    for (int row = 0; row < keep; ++row) {
        const int from = physicalRow(firstKept + row);
        if (m_stride > 0) {
            const double *source = m_cells.constData() + qsizetype(from) * m_stride;
            double *target = cells.data() + qsizetype(row) * newStride;
            std::copy(source, source + qMin(m_stride, newStride), target);
        }
        stamps[row] = m_stamps.at(from);
    }

    m_cells = std::move(cells);
    m_stamps = std::move(stamps);
    m_capacity = newCapacity;
    m_stride = newStride;
    m_head = 0;
    m_size = keep;

    invalidateStats();
}

bool SampleBuffer::append(qint64 monotonicNs, const double *values, int count)
{
    if (count <= 0 || count > m_columnLimit || !values)
        return false;

    if (count > m_stride)
        growStride(count);

    // Обратная метка сделала бы преобразование по X немонотонным и сложила бы кривую саму
    // на себя. Устройство иногда переставляет строки местами при переполнении очереди —
    // это данные, а не повод их потерять, поэтому метку зажимаем, а строку берём.
    if (m_size > 0)
        monotonicNs = qMax(monotonicNs, timestamp(m_size - 1));

    int row = 0;
    if (m_size < m_capacity) {
        row = physicalRow(m_size);
        ++m_size;
    } else {
        row = m_head;
        m_head = (m_head + 1) % m_capacity;
    }

    double *cells = m_cells.data() + qsizetype(row) * m_stride;
    for (int column = 0; column < m_stride; ++column)
        cells[column] = (column < count) ? values[column] : qQNaN();
    m_stamps[row] = monotonicNs;

    invalidateStats();
    return true;
}

void SampleBuffer::clearSamples()
{
    m_head = 0;
    m_size = 0;
    invalidateStats();
}

void SampleBuffer::clearColumn(int column)
{
    if (column < 0 || column >= m_stride)
        return;

    for (int row = 0; row < m_size; ++row)
        m_cells[qsizetype(physicalRow(row)) * m_stride + column] = qQNaN();

    invalidateStats();
}

void SampleBuffer::reset()
{
    m_cells.clear();
    // fill(), а не assign(): последний появился в Qt 6.6, а минимум проекта — 6.5.
    m_stamps.fill(0, m_capacity);
    m_head = 0;
    m_size = 0;
    m_stride = 0;
    invalidateStats();
}

void SampleBuffer::invalidateStats()
{
    m_statsCache.fill(ColumnStats{}, m_stride);
    m_statsValid.fill(false, m_stride);
}

SampleBuffer::ColumnStats SampleBuffer::stats(int column) const
{
    if (column < 0 || column >= m_stride)
        return {};

    if (m_statsValid.size() == m_stride && m_statsValid.at(column))
        return m_statsCache.at(column);

    const ColumnStats computed = stats(column, 0, m_size);

    if (m_statsValid.size() == m_stride) {
        m_statsCache[column] = computed;
        m_statsValid[column] = true;
    }
    return computed;
}

SampleBuffer::ColumnStats SampleBuffer::stats(int column, int fromRow, int count) const
{
    ColumnStats result;
    if (column < 0 || column >= m_stride)
        return result;

    const int first = qMax(0, fromRow);
    const int last = qMin(m_size, first + qMax(0, count));

    double minimum = 0.0;
    double maximum = 0.0;
    double sum = 0.0;

    // Один проход на три числа вместо трёх независимых обходов, как было раньше.
    for (int row = first; row < last; ++row) {
        const double value = at(row, column);
        if (!qIsFinite(value))
            continue;

        if (result.finiteCount == 0) {
            minimum = value;
            maximum = value;
        } else {
            minimum = qMin(minimum, value);
            maximum = qMax(maximum, value);
        }
        sum += value;
        ++result.finiteCount;
    }

    if (result.finiteCount > 0) {
        result.minimum = minimum;
        result.maximum = maximum;
        result.mean = sum / double(result.finiteCount);
    }
    return result;
}

QString SampleBuffer::toCsv(const QStringList &names) const
{
    if (m_stride == 0)
        return {};

    QStringList header{QStringLiteral("time_ms")};
    for (int column = 0; column < m_stride; ++column) {
        header << (column < names.size() ? names.at(column)
                                         : QString::number(column + 1));
    }

    QStringList lines{header.join(u',')};

    // Строк ровно столько, сколько отсчётов: разной длины у колонок больше не бывает.
    const qint64 origin = m_size > 0 ? timestamp(0) : 0;

    for (int row = 0; row < m_size; ++row) {
        QStringList fields;
        fields.reserve(m_stride + 1);
        fields << QString::number((timestamp(row) - origin) / 1'000'000);
        for (int column = 0; column < m_stride; ++column) {
            const double value = at(row, column);
            // Пропуск выходит пустым полем, а не «nan»: пустое поле понимают все читатели
            // CSV, а «nan» половина из них принимает за текст и роняет тип колонки.
            fields << (qIsFinite(value) ? QString::number(value) : QString());
        }
        lines << fields.join(u',');
    }

    return lines.join(u'\n');
}

} // namespace spotty
