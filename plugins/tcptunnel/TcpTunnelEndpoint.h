/**
 * \file TcpTunnelEndpoint.h
 * \brief Адрес подключения и всё, что из него выводится: подпись, выжимка, дескриптор.
 */
#pragma once

#include "HostNames.h"
#include "TcpTunnelSettings.h"

#include <spotty/api/InterfaceDescriptor.h>

#include <QFileInfo>
#include <QString>
#include <QVariantMap>

namespace spotty::tcptunnel {

/**
 * \brief Адрес в виде, пригодном для подписи: `host:port`.
 * \param settings Настройки подключения.
 * \return Пустую строку, если адрес ещё не введён.
 *
 * IPv6-адрес берётся в квадратные скобки, иначе двоеточия внутри него не отличить от
 * двоеточия перед портом.
 */
inline QString endpointText(const QVariantMap &settings)
{
    const QString host = settings.value(QLatin1String(kHost)).toString().trimmed();
    if (host.isEmpty())
        return {};

    const int port = settings.value(QLatin1String(kPort), kDefaultPort).toInt();
    const QString shown = host.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(host) : host;
    return QStringLiteral("%1:%2").arg(shown).arg(port);
}

/// \brief Имя выбранного порта удалённой машины (`ttyUSB0`); пусто, пока порт не выбран.
inline QString remoteUartName(const QVariantMap &settings)
{
    return QFileInfo(settings.value(QLatin1String(kRemoteUart)).toString()).fileName();
}

/**
 * \brief Описание подключения для списка интерфейсов.
 * \param settings Настройки подключения.
 * \param emptyName Подпись, пока адрес не введён (через tr() у вызывающего).
 * \param names Известные имена хостов; `nullptr` — не использовать.
 *
 * Системное имя — настоящий адрес `адрес:порт`. Подпись, она же псевдоним по умолчанию, —
 * `имя ttyUSB0`: имя — то, под которым машина знает себя (адрес менялся бы вместе с выдачей DHCP,
 * а `raspberrypi` читается), порт — тот UART, который подключение отдаёт. Пока имя неизвестно, вместо
 * него адрес. Пока порт не выбран, подпись `имя:порт` (TCP-порт агента), а без имени — просто адрес.
 */
inline InterfaceDescriptor describeEndpoint(const QVariantMap &settings, const QString &emptyName,
                                            const HostNames *names = nullptr)
{
    InterfaceDescriptor descriptor;
    const QString endpoint = endpointText(settings);
    descriptor.systemName = endpoint.isEmpty() ? emptyName : endpoint;
    if (endpoint.isEmpty())
        return descriptor;

    const QString host = settings.value(QLatin1String(kHost)).toString().trimmed();
    const QString name = names ? names->nameFor(host) : QString();
    const QString uart = remoteUartName(settings);
    if (!uart.isEmpty()) {
        descriptor.description = QStringLiteral("%1 %2").arg(name.isEmpty() ? host : name, uart);
    } else if (!name.isEmpty()) {
        descriptor.description =
            QStringLiteral("%1:%2").arg(name).arg(settings.value(QLatin1String(kPort), kDefaultPort).toInt());
    }
    return descriptor;
}

/**
 * \brief Выжимка настроек порта машины для списка: `115200 8N1`.
 * \return Пустую строку, пока порт не выбран, — тогда настройки порта ничего не значат.
 *
 * Имя порта сюда не входит: оно уже в подписи по умолчанию (см. describeEndpoint()), и в списке
 * стояло бы дважды.
 */
inline QString uartSummary(const QVariantMap &settings)
{
    if (remoteUartName(settings).isEmpty())
        return {};
    return QStringLiteral("%1 %2")
        .arg(settings.value(QLatin1String(kBaud), 115200).toInt())
        .arg(settings.value(QLatin1String(kFormat), QStringLiteral("8N1")).toString().toUpper());
}

} // namespace spotty::tcptunnel
