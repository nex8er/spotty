/**
 * \file HostNames.cpp
 * \brief Реализация spotty::tcptunnel::HostNames.
 */
#include "HostNames.h"

#include <QHostAddress>

namespace spotty::tcptunnel {

HostNames::HostNames(QString filePath)
    : TabFileStore(QStringLiteral("address<TAB>machine name - names Spotty has learned"))
{
    setFile(filePath);
}

QString HostNames::shortName(const QString &name)
{
    const QString trimmed = name.trimmed();
    // Адрес (v4 или v6) именем не считается: обратный поиск без записи возвращает его самого.
    if (trimmed.isEmpty() || !QHostAddress(trimmed).isNull())
        return {};
    return trimmed.section(QLatin1Char('.'), 0, 0);
}

QString HostNames::nameFor(const QString &host) const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_entries.find(host.trimmed().toLower());
    return it == m_entries.end() ? QString() : it->second;
}

bool HostNames::remember(const QString &host, const QString &name)
{
    const QString key = host.trimmed().toLower();
    const QString shortened = shortName(name);
    // Если вместо адреса ввели имя («pi-lab»), оно уже и есть подпись; лишний вариант не нужен.
    if (key.isEmpty() || shortened.isEmpty() || shortened.compare(key, Qt::CaseInsensitive) == 0)
        return false;

    QMutexLocker lock(&m_mutex);
    QString &stored = m_entries[key];
    if (stored == shortened)
        return false;
    stored = shortened;
    save();
    return true;
}

} // namespace spotty::tcptunnel
