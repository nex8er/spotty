/**
 * \file JlinkDeviceDatabase.cpp
 * \brief Реализация spotty::JlinkDeviceDatabase.
 */
#include "JlinkDeviceDatabase.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>

namespace spotty {

namespace {

Q_LOGGING_CATEGORY(lcJlinkDevices, "spotty.plugins.jlinkrtt")

} // namespace

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

    // Тот же принцип поиска, что и у самой библиотеки (JlinkArmLibrary): установщик
    // J-Link software не кладёт свои файлы в системные пути, только в собственный каталог.
    const QStringList candidates = {
#if defined(Q_OS_MACOS)
        QStringLiteral("/Applications/SEGGER/JLink/JLinkDevices.xml"),
#elif defined(Q_OS_WIN)
        QStringLiteral("C:/Program Files/SEGGER/JLink/JLinkDevices.xml"),
        QStringLiteral("C:/Program Files (x86)/SEGGER/JLink/JLinkDevices.xml"),
#else
        QStringLiteral("/opt/SEGGER/JLink/JLinkDevices.xml"),
#endif
    };

    QString path;
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            path = candidate;
            break;
        }
    }

    if (path.isEmpty()) {
        qCWarning(lcJlinkDevices) << "JLinkDevices.xml not found - the target device field "
                                     "will have no autocomplete suggestions";
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcJlinkDevices) << "cannot read" << path << file.errorString();
        return;
    }

    QSet<QString> seen;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("ChipInfo"))
            continue;

        const QString name = xml.attributes().value(QLatin1String("Name")).toString();
        if (!name.isEmpty() && !seen.contains(name)) {
            seen.insert(name);
            m_names.append(name);
        }
    }

    if (xml.hasError()) {
        qCWarning(lcJlinkDevices) << "malformed" << path << xml.errorString()
                                  << "- using" << m_names.size() << "entries read so far";
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
