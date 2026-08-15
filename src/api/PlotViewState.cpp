/**
 * \file PlotViewState.cpp
 * \brief Реализация spotty::PlotViewState.
 */
#include <spotty/data/PlotViewState.h>

#include <QtGlobal>

#include <algorithm>

namespace spotty {

namespace {

/// \brief Наименьшая длительность окна, нс. Микросекунда — предел разумного приближения.
constexpr qint64 kMinimumDuration = 1'000LL;

/// \brief Наибольшая длительность окна, нс. Сутки.
constexpr qint64 kMaximumDuration = 86'400'000'000'000LL;

/// \name Пределы вертикального масштаба
/// @{
constexpr double kMinimumVerticalZoom = 0.05;
constexpr double kMaximumVerticalZoom = 50.0;
/// @}

/**
 * \brief Насколько близко к правому краю считается «доехал».
 *
 * Доля длительности окна. Требовать попадания в точку значило бы, что слежение почти
 * никогда не возвращается само: последний отсчёт приходит уже после того, как отпущена
 * мышь.
 */
constexpr double kFollowSnapFraction = 0.02;

} // namespace

PlotViewState::PlotViewState(QObject *parent)
    : QObject(parent)
{
}

void PlotViewState::setWindow(qint64 from, qint64 to)
{
    if (to <= from)
        to = from + kMinimumDuration;

    const qint64 duration = qBound(kMinimumDuration, to - from, kMaximumDuration);
    to = from + duration;

    if (from == m_from && to == m_to)
        return;

    m_from = from;
    m_to = to;
    Q_EMIT changed();
}

void PlotViewState::setWindowDuration(qint64 nanoseconds)
{
    const qint64 duration = qBound(kMinimumDuration, nanoseconds, kMaximumDuration);
    // Правый край на месте: длительность меняют, чтобы увидеть больше прошлого, а не
    // чтобы уехать от свежих данных.
    setWindow(m_to - duration, m_to);
}

void PlotViewState::setFollowing(bool following)
{
    if (m_following == following)
        return;
    m_following = following;
    Q_EMIT followingChanged(following);
    Q_EMIT changed();
}

void PlotViewState::panBy(qint64 nanoseconds)
{
    if (nanoseconds == 0)
        return;

    // Ручная прокрутка снимает слежение: иначе окно тут же уехало бы обратно, и потащить
    // график назад было бы невозможно.
    setFollowing(false);
    setWindow(m_from + nanoseconds, m_to + nanoseconds);
}

void PlotViewState::zoomX(double factor, qint64 anchor)
{
    if (factor <= 0.0 || qFuzzyCompare(factor, 1.0))
        return;

    const qint64 duration = windowDuration();
    const qint64 wanted = qBound(kMinimumDuration, qint64(double(duration) * factor),
                                 kMaximumDuration);
    if (wanted == duration)
        return;

    // Доля, на которой стоит точка привязки, сохраняется — значит, точка остаётся под
    // курсором, а окно растёт или сжимается вокруг неё.
    const double fraction =
        duration > 0 ? qBound(0.0, double(anchor - m_from) / double(duration), 1.0) : 0.5;

    const qint64 from = anchor - qint64(double(wanted) * fraction);
    setWindow(from, from + wanted);
}

void PlotViewState::zoomY(double factor)
{
    if (factor <= 0.0)
        return;

    const double wanted =
        qBound(kMinimumVerticalZoom, m_verticalZoom * factor, kMaximumVerticalZoom);
    if (qFuzzyCompare(wanted, m_verticalZoom))
        return;

    m_verticalZoom = wanted;
    Q_EMIT changed();
}

void PlotViewState::panY(double fraction)
{
    if (qFuzzyIsNull(fraction))
        return;
    m_verticalOffset += fraction;
    Q_EMIT changed();
}

void PlotViewState::resetVertical()
{
    if (qFuzzyCompare(m_verticalZoom, 1.0) && qFuzzyIsNull(m_verticalOffset))
        return;
    m_verticalZoom = 1.0;
    m_verticalOffset = 0.0;
    Q_EMIT changed();
}

void PlotViewState::clampTo(qint64 first, qint64 last)
{
    if (last <= first)
        return;

    const qint64 duration = windowDuration();

    if (m_following) {
        setWindow(last - duration, last);
        return;
    }

    // Уехав за края буфера, смотреть нечего — там заведомо пусто.
    if (m_to > last) {
        setWindow(last - duration, last);
    } else if (m_from < first) {
        setWindow(first, first + duration);
    }

    // Доводка до правого края возвращает слежение — тот же жест, что в терминале.
    if (!m_following && last - m_to <= qint64(double(duration) * kFollowSnapFraction))
        setFollowing(true);
}

void PlotViewState::setActiveSeries(int index)
{
    if (m_active == index)
        return;
    m_active = index;
    Q_EMIT changed();
}

bool PlotViewState::isInScaleGroup(int index) const
{
    return hasScaleGroup() && m_selection.contains(index);
}

void PlotViewState::setSelectionGroup(const QList<int> &series)
{
    QList<int> sorted = series;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    if (sorted == m_selection)
        return;

    m_selection = sorted;

    // Пока группа существует, активный ряд обязан быть её членом. Без этого слева была бы
    // подписана шкала ряда, который в общую шкалу не входит, — то есть подпись не
    // соответствовала бы ни одной нарисованной кривой.
    if (hasScaleGroup() && !m_selection.contains(m_active))
        m_active = m_selection.first();

    Q_EMIT changed();
}

void PlotViewState::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    Q_EMIT changed();
}

void PlotViewState::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    Q_EMIT pausedChanged(paused);
    Q_EMIT changed();
}

} // namespace spotty
