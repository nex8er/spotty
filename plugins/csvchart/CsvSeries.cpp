/**
 * \file CsvSeries.cpp
 * \brief Реализация spotty::CsvSeries.
 */
#include "CsvSeries.h"

#include <algorithm>
#include <limits>

namespace spotty {

namespace {

/**
 * \brief Цвета рядов.
 *
 * Своя палитра, а не роли темы: «цвет второго ряда» — не роль интерфейса, и тема о нём
 * знать не должна. Порядок подобран так, чтобы соседние ряды различались и в оттенках
 * серого — распечатанный график тоже читают.
 */
const QColor kSeriesColors[] = {
    QColor(0x6C, 0xB6, 0xFF), QColor(0x8D, 0xDB, 0x8C), QColor(0xE8, 0xC4, 0x6A),
    QColor(0xE0, 0x8A, 0x8A), QColor(0xC0, 0x9C, 0xE8), QColor(0x7A, 0xD6, 0xD6),
};

} // namespace

QColor CsvSeries::defaultColor(int index)
{
    constexpr int count = int(std::size(kSeriesColors));
    return kSeriesColors[index % count];
}

bool CsvSeries::feed(const QString &line, qint64 monotonicNs)
{
    const QStringList parts = line.split(m_separator, Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;

    QList<double> parsed;
    parsed.reserve(parts.size());
    for (const QString &part : parts) {
        bool ok = false;
        const double value = part.trimmed().toDouble(&ok);
        if (!ok)
            return false; // Строка не про данные — например, «boot ok».
        parsed.append(value);
    }

    const bool grew = parsed.size() > m_series.size();
    while (m_series.size() < parsed.size()) {
        Series series;
        series.color = defaultColor(int(m_series.size()));
        series.name = tr("Column %1").arg(m_series.size() + 1);
        m_series.append(series);
    }

    for (int i = 0; i < parsed.size(); ++i) {
        QList<double> &values = m_series[i].values;
        values.append(parsed.at(i));
        // Подрезаем спереди: график показывает хвост потока, а неограниченный рост съел бы
        // память на длинной сессии — устройство шлёт строки часами.
        while (values.size() > m_capacity)
            values.removeFirst();
    }

    m_timestamps.append(monotonicNs);
    while (m_timestamps.size() > m_capacity)
        m_timestamps.removeFirst();

    if (grew)
        Q_EMIT seriesAdded();
    Q_EMIT changed();
    return true;
}

void CsvSeries::clear()
{
    m_series.clear();
    m_timestamps.clear();
    m_xAxis = -1;
    Q_EMIT seriesAdded();
    Q_EMIT changed();
}

void CsvSeries::setSeriesVisible(int index, bool visible)
{
    if (index < 0 || index >= m_series.size())
        return;
    m_series[index].visible = visible;
    Q_EMIT changed();
}

void CsvSeries::setSeriesColor(int index, const QColor &color)
{
    if (index < 0 || index >= m_series.size())
        return;
    m_series[index].color = color;
    Q_EMIT changed();
}

void CsvSeries::setXAxisSeries(int index)
{
    m_xAxis = (index >= 0 && index < m_series.size()) ? index : -1;
    Q_EMIT changed();
}

void CsvSeries::setCapacity(int points)
{
    m_capacity = qMax(2, points);
    for (Series &series : m_series) {
        while (series.values.size() > m_capacity)
            series.values.removeFirst();
    }
    while (m_timestamps.size() > m_capacity)
        m_timestamps.removeFirst();
    Q_EMIT changed();
}

void CsvSeries::range(double *minimum, double *maximum) const
{
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();

    for (int i = 0; i < m_series.size(); ++i) {
        // Скрытый ряд не участвует: иначе выключение выброса не меняло бы масштаб, а
        // ради этого его чаще всего и выключают.
        if (!m_series.at(i).visible || i == m_xAxis)
            continue;
        for (const double value : m_series.at(i).values) {
            lo = qMin(lo, value);
            hi = qMax(hi, value);
        }
    }

    if (lo > hi) {
        lo = 0.0;
        hi = 1.0;
    } else if (qFuzzyCompare(lo, hi)) {
        // Постоянный сигнал: без запаса линия легла бы точно на край и стала невидимой.
        lo -= 1.0;
        hi += 1.0;
    }

    *minimum = lo;
    *maximum = hi;
}

double CsvSeries::minimumOf(int index) const
{
    if (index < 0 || index >= m_series.size() || m_series.at(index).values.isEmpty())
        return 0.0;
    return *std::min_element(m_series.at(index).values.cbegin(),
                             m_series.at(index).values.cend());
}

double CsvSeries::maximumOf(int index) const
{
    if (index < 0 || index >= m_series.size() || m_series.at(index).values.isEmpty())
        return 0.0;
    return *std::max_element(m_series.at(index).values.cbegin(),
                             m_series.at(index).values.cend());
}

double CsvSeries::averageOf(int index) const
{
    if (index < 0 || index >= m_series.size() || m_series.at(index).values.isEmpty())
        return 0.0;
    const QList<double> &values = m_series.at(index).values;
    double sum = 0.0;
    for (const double value : values)
        sum += value;
    return sum / double(values.size());
}

QString CsvSeries::toCsv() const
{
    if (m_series.isEmpty())
        return {};

    QStringList header{QStringLiteral("time_ms")};
    for (const Series &series : m_series)
        header << series.name;

    QStringList lines{header.join(u',')};

    // Длина рядов может различаться на единицу: строка, пришедшая с меньшим числом полей,
    // не удлиняет остальные. Идём по самому длинному и оставляем пропуски пустыми — так
    // это и понимает всякая программа, читающая CSV.
    qsizetype longest = m_timestamps.size();
    for (const Series &series : m_series)
        longest = qMax(longest, series.values.size());

    const qint64 origin = m_timestamps.isEmpty() ? 0 : m_timestamps.first();

    for (qsizetype row = 0; row < longest; ++row) {
        QStringList fields;
        fields << (row < m_timestamps.size()
                       ? QString::number((m_timestamps.at(row) - origin) / 1'000'000)
                       : QString());
        for (const Series &series : m_series) {
            fields << (row < series.values.size() ? QString::number(series.values.at(row))
                                                  : QString());
        }
        lines << fields.join(u',');
    }

    return lines.join(u'\n');
}

} // namespace spotty
