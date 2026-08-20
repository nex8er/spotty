/**
 * \file NabPlugin.cpp
 * \brief Реализация spotty::NabPlugin.
 */
#include "NabPlugin.h"

#include "LibusbKLibrary.h"
#include "NabChannel.h"

namespace spotty {

namespace {

constexpr auto kIdPrefix = "nab:28e9:325a:";

QString encodeIdentity(const QString &identity)
{
    return QString::fromLatin1(identity.toUtf8().toHex());
}

QString buildId(const LibusbKLibrary::InterfaceInfo &info)
{
    return QStringLiteral("%1%2:%3")
        .arg(QLatin1String(kIdPrefix), encodeIdentity(info.identity))
        .arg(info.interfaceNumber);
}

bool parseId(const QString &id, QString *identity, int *interfaceNumber)
{
    if (!id.startsWith(QLatin1String(kIdPrefix)))
        return false;

    const QString encoded = id.mid(qstrlen(kIdPrefix));
    const qsizetype separator = encoded.lastIndexOf(QLatin1Char(':'));
    if (separator <= 0)
        return false;

    bool numberOk = false;
    const int number = encoded.sliced(separator + 1).toInt(&numberOk);
    const QByteArray rawIdentity = QByteArray::fromHex(encoded.left(separator).toLatin1());
    if (!numberOk || number < 0 || rawIdentity.isEmpty())
        return false;

    if (identity)
        *identity = QString::fromUtf8(rawIdentity);
    if (interfaceNumber)
        *interfaceNumber = number;
    return true;
}

QString versionText(quint16 bcd)
{
    return QStringLiteral("%1.%2")
        .arg((bcd >> 8) & 0xFF)
        .arg((bcd >> 4) & 0x0F, 2, 10, QLatin1Char('0'));
}

QString endpointText(const LibusbKLibrary::PipeInfo &pipe)
{
    return QStringLiteral("0x%1 (%2 bytes)")
        .arg(pipe.address, 2, 16, QLatin1Char('0'))
        .arg(pipe.maxPacketSize);
}

} // namespace

QList<InterfaceDescriptor> NabPlugin::enumerate() const
{
    QList<InterfaceDescriptor> result;
    const QList<LibusbKLibrary::InterfaceInfo> interfaces =
        LibusbKLibrary::instance().enumerateNabInterfaces();
    result.reserve(interfaces.size());

    for (const LibusbKLibrary::InterfaceInfo &info : interfaces) {
        InterfaceDescriptor descriptor;
        descriptor.id = buildId(info);
        descriptor.systemName = tr("USB interface %1").arg(info.interfaceNumber);
        descriptor.description = !info.interfaceName.isEmpty()
                                     ? info.interfaceName
                                     : (!info.product.isEmpty()
                                            ? info.product
                                            : tr("NAB USB interface %1").arg(info.interfaceNumber));
        descriptor.serialNumber = info.serialNumber;
        descriptor.extra.insert(QStringLiteral("vendorId"), QStringLiteral("0x28e9"));
        descriptor.extra.insert(QStringLiteral("productId"), QStringLiteral("0x325a"));
        descriptor.extra.insert(QStringLiteral("usbVersion"), versionText(info.usbVersion));
        descriptor.extra.insert(QStringLiteral("deviceVersion"), versionText(info.deviceVersion));
        descriptor.extra.insert(QStringLiteral("interface"), info.interfaceNumber);
        descriptor.extra.insert(QStringLiteral("interfaceClass"),
                                QStringLiteral("0x%1 / 0x%2 / 0x%3")
                                    .arg(info.interfaceClass, 2, 16, QLatin1Char('0'))
                                    .arg(info.interfaceSubClass, 2, 16, QLatin1Char('0'))
                                    .arg(info.interfaceProtocol, 2, 16, QLatin1Char('0')));
        descriptor.extra.insert(QStringLiteral("bulkIn"), endpointText(info.input));
        descriptor.extra.insert(QStringLiteral("bulkOut"), endpointText(info.output));
        if (!info.manufacturer.isEmpty())
            descriptor.extra.insert(QStringLiteral("manufacturer"), info.manufacturer);
        if (!info.product.isEmpty())
            descriptor.extra.insert(QStringLiteral("product"), info.product);
        if (!info.interfaceName.isEmpty())
            descriptor.extra.insert(QStringLiteral("interfaceName"), info.interfaceName);
        result.append(descriptor);
    }

    return result;
}

IInterfaceChannel *NabPlugin::createChannel(const InterfaceDescriptor &descriptor)
{
    QString identity;
    int interfaceNumber = -1;
    if (!parseId(descriptor.id, &identity, &interfaceNumber))
        return nullptr;

    return new NabChannel(identity, interfaceNumber);
}

} // namespace spotty
