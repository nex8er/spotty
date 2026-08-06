/**
 * \file JlinkDeviceDatabase.cpp
 * \brief Реализация spotty::JlinkDeviceDatabase.
 */
#include "JlinkDeviceDatabase.h"

#include "JlinkArmLibrary.h"

#include <QSet>

#include <algorithm>

namespace spotty {

JlinkDeviceDatabase &JlinkDeviceDatabase::instance()
{
    static JlinkDeviceDatabase database;
    return database;
}

void JlinkDeviceDatabase::load()
{
    if (m_loaded)
        return;
    m_loaded = true;

    const QList<JlinkArmLibrary::DeviceInfo> devices = JlinkArmLibrary::instance().knownDevices();

    QSet<QString> seen;
    seen.reserve(devices.size());
    m_names.reserve(devices.size());
    for (const JlinkArmLibrary::DeviceInfo &device : devices) {
        if (device.name.isEmpty() || seen.contains(device.name))
            continue;
        seen.insert(device.name);
        m_names.append(device.name);
    }

    std::sort(m_names.begin(), m_names.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
}

const QStringList &JlinkDeviceDatabase::deviceNames()
{
    load();
    return m_names;
}

} // namespace spotty
