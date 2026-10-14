/**
 * \file KnownHosts.cpp
 * \brief Реализация spotty::tcptunnel::KnownHosts.
 */
#include "KnownHosts.h"

namespace spotty::tcptunnel {

KnownHosts::KnownHosts(QString filePath)
    : TabFileStore(QStringLiteral("host:port<TAB>fingerprint - keys Spotty has been told to trust"))
{
    setFile(filePath);
}

QString KnownHosts::keyFor(const QString &host, int port)
{
    // Имя хоста регистра не различает; запись «Pi» и «pi» — одна и та же машина.
    return QStringLiteral("%1:%2").arg(host.trimmed().toLower()).arg(port);
}

KnownHosts::Match KnownHosts::check(const QString &host, int port, const QString &fingerprint) const
{
    const QString stored = this->fingerprint(host, port);
    return stored.isEmpty() ? Match::Unknown : (stored == fingerprint ? Match::Same : Match::Different);
}

bool KnownHosts::remember(const QString &host, int port, const QString &fingerprint)
{
    QMutexLocker lock(&m_mutex);
    m_entries[keyFor(host, port)] = fingerprint;
    return save();
}

bool KnownHosts::forget(const QString &host, int port)
{
    QMutexLocker lock(&m_mutex);
    if (m_entries.erase(keyFor(host, port)) == 0)
        return false;
    save();
    return true;
}

QString KnownHosts::fingerprint(const QString &host, int port) const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_entries.find(keyFor(host, port));
    return it == m_entries.end() ? QString() : it->second;
}

} // namespace spotty::tcptunnel
