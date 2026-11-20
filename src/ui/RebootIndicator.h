/** \file RebootIndicator.h
 * \brief Быстрое подтверждение не должно скрывать индикацию до первого кадра.
 */
#pragma once
#include <QtGlobal>
#include <limits>

namespace spotty {
class RebootIndicator
{
public:
    RebootIndicator(qint64 now, int timeout)
        : m_until(timeout > 0 ? now + timeout : std::numeric_limits<qint64>::max())
        , m_visibleUntil(qMin(m_until, now + 500))
    {}

    void answered(qint64 now) { m_until = qMin(m_until, qMax(now, m_visibleUntil)); }
    bool active(qint64 now) const { return now < m_until; }

private:
    qint64 m_until;
    qint64 m_visibleUntil;
};
} // namespace spotty
