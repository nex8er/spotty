/**
 * \file PlotModel.cpp
 * \brief Реализация spotty::PlotModel.
 */
#include <spotty/data/PlotModel.h>
#include <spotty/data/PlotFormat.h>

#include <QtGlobal>

namespace spotty {

namespace {

/**
 * \brief Цвета рядов, `0xAARRGGBB`.
 *
 * Своя палитра, а не роли темы: «цвет второго ряда» — не роль интерфейса, и тема о нём
 * знать не должна. Порядок подобран так, чтобы соседние ряды различались и в оттенках
 * серого — распечатанный график тоже читают.
 */
constexpr quint32 kSeriesColors[] = {
    0xFF6CB6FF, 0xFF8DDB8C, 0xFFE8C46A, 0xFFE08A8A, 0xFFC09CE8, 0xFF7AD6D6,
};

} // namespace

PlotModel::PlotModel(QObject *parent)
    : QObject(parent)
{
}

quint32 PlotModel::defaultColor(int index)
{
    constexpr int count = int(std::size(kSeriesColors));
    return kSeriesColors[index % count];
}

bool PlotModel::growSeries(int columns)
{
    if (columns <= m_series.size())
        return false;

    while (m_series.size() < columns) {
        PlotSeries series;
        series.color = defaultColor(int(m_series.size()));
        const int index = int(m_series.size());
        const QString sourceName = m_reportedNames.value(index);
        series.name = sourceName.isEmpty() ? PlotFormat::defaultSeriesName(index) : sourceName;
        m_series.append(series);
    }
    return true;
}

bool PlotModel::feed(const QString &line, qint64 monotonicNs)
{
    QList<double> values;
    QStringList names;

    const SampleParser::Result result =
        m_parser.parse(line, m_samples.columnCount(), &values, &names);

    if (result.outcome == SampleParser::Outcome::Header) {
        // Заголовок не создаёт колонок сам, но запоминается и до первой строки данных:
        // иначе профиль, сброшенный после раннего заголовка, мог вернуть только alpha,
        // bravo, хотя устройство уже назвало поля.
        m_reportedNames = names;
        bool renamed = false;
        for (int i = 0; i < names.size() && i < m_series.size(); ++i) {
            PlotSeries &series = m_series[i];
            const QString sourceName = m_reportedNames.at(i);
            if (sourceName.isEmpty() || series.nameIsCustom || series.name == sourceName)
                continue;
            series.name = sourceName;
            renamed = true;
        }
        if (renamed)
            Q_EMIT seriesAdded();
        return false;
    }

    if (result.outcome != SampleParser::Outcome::Data)
        return false;

    if (!m_samples.append(monotonicNs, values.constData(), int(values.size()))) {
        ++m_rejected;
        return false;
    }

    const bool grew = growSeries(m_samples.columnCount());
    if (grew)
        Q_EMIT seriesAdded();
    Q_EMIT changed();
    return true;
}

void PlotModel::clearSamples()
{
    m_samples.clearSamples();
    Q_EMIT changed();
}

void PlotModel::clearColumn(int column)
{
    m_samples.clearColumn(column);
    Q_EMIT changed();
}

void PlotModel::setSeriesVisible(int index, bool visible)
{
    if (index < 0 || index >= m_series.size() || m_series.at(index).visible == visible)
        return;
    m_series[index].visible = visible;
    Q_EMIT changed();
    Q_EMIT configurationChanged();
}

void PlotModel::setSeriesColor(int index, quint32 color)
{
    if (index < 0 || index >= m_series.size() || m_series.at(index).color == color)
        return;
    m_series[index].color = color;
    Q_EMIT changed();
    Q_EMIT configurationChanged();
}

void PlotModel::setSeriesName(int index, const QString &name)
{
    if (index < 0 || index >= m_series.size())
        return;

    const QString trimmed = name.trimmed();
    // Пустое имя возвращает ряд к имени по умолчанию и снимает признак ручной правки:
    // иначе стерев имя, пользователь получил бы безымянную строку без пути назад.
    if (trimmed.isEmpty()) {
        const QString sourceName = m_reportedNames.value(index);
        m_series[index].name = sourceName.isEmpty() ? PlotFormat::defaultSeriesName(index)
                                                     : sourceName;
        m_series[index].nameIsCustom = false;
    } else {
        m_series[index].name = trimmed;
        m_series[index].nameIsCustom = true;
    }
    Q_EMIT seriesAdded();
    Q_EMIT configurationChanged();
}

void PlotModel::resetSeriesConfiguration()
{
    bool didChange = false;
    for (int index = 0; index < m_series.size(); ++index) {
        PlotSeries &series = m_series[index];
        const QString reportedName = m_reportedNames.value(index);
        const QString sourceName = reportedName.isEmpty() ? PlotFormat::defaultSeriesName(index)
                                                           : reportedName;
        if (series.name != sourceName || series.nameIsCustom
            || series.color != defaultColor(index) || !series.visible || series.hasCustomRange) {
            didChange = true;
        }
        series.name = sourceName;
        series.nameIsCustom = false;
        series.color = defaultColor(index);
        series.visible = true;
        series.hasCustomRange = false;
        series.customMinimum = 0.0;
        series.customMaximum = 1.0;
    }

    if (!didChange)
        return;
    Q_EMIT seriesAdded();
    Q_EMIT changed();
    Q_EMIT configurationChanged();
}

void PlotModel::setSeriesRange(int index, bool hasRange, double minimum, double maximum)
{
    if (index < 0 || index >= m_series.size())
        return;

    m_series[index].hasCustomRange = hasRange;
    if (hasRange) {
        // Перевёрнутый или вырожденный отрезок означал бы деление на ноль в преобразовании
        // координат; поправить его тише, чем отказать, — пользователь видит результат сразу.
        m_series[index].customMinimum = qMin(minimum, maximum);
        m_series[index].customMaximum = qMax(minimum, maximum);
        if (qFuzzyCompare(m_series[index].customMinimum, m_series[index].customMaximum))
            m_series[index].customMaximum = m_series[index].customMinimum + 1.0;
    }
    Q_EMIT changed();
    Q_EMIT configurationChanged();
}

QStringList PlotModel::seriesNames() const
{
    QStringList names;
    names.reserve(m_series.size());
    for (const PlotSeries &series : m_series)
        names.append(series.name);
    return names;
}

void PlotModel::setXAxisSeries(int index)
{
    const int wanted = (index >= 0 && index < m_series.size()) ? index : -1;
    if (wanted == m_xAxis)
        return;
    m_xAxis = wanted;
    Q_EMIT changed();
    Q_EMIT configurationChanged();
}

void PlotModel::setCapacity(int samples)
{
    if (samples == m_samples.capacity())
        return;
    m_samples.setCapacity(samples);
    Q_EMIT changed();
    Q_EMIT configurationChanged();
}

void PlotModel::setSeparator(QChar separator)
{
    if (separator == m_parser.separator())
        return;
    m_parser.setSeparator(separator);
    Q_EMIT changed();
    Q_EMIT configurationChanged();
}

} // namespace spotty
