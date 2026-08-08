/**
 * \file CsvSeries.cpp
 * \brief Реализация spotty::CsvSeries.
 */
#include "CsvSeries.h"

#include <limits>

namespace spotty {

bool CsvSeries::feed(const QString &line)
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

    while (m_series.size() < parsed.size())
        m_series.append(QList<double>{});

    for (int i = 0; i < parsed.size(); ++i) {
        QList<double> &series = m_series[i];
        series.append(parsed.at(i));
        // Подрезаем спереди: график показывает хвост потока, а неограниченный рост съел бы
        // память на длинной сессии — устройство шлёт строки часами.
        while (series.size() > m_capacity)
            series.removeFirst();
    }

    Q_EMIT changed();
    return true;
}

void CsvSeries::clear()
{
    m_series.clear();
    Q_EMIT changed();
}

void CsvSeries::setCapacity(int points)
{
    m_capacity = qMax(2, points);
    for (QList<double> &series : m_series) {
        while (series.size() > m_capacity)
            series.removeFirst();
    }
    Q_EMIT changed();
}

void CsvSeries::range(double *minimum, double *maximum) const
{
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();

    for (const QList<double> &series : m_series) {
        for (const double value : series) {
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

} // namespace spotty
