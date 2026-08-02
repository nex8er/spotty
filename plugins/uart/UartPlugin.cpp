/**
 * \file UartPlugin.cpp
 * \brief Реализация spotty::UartPlugin.
 */
#include "UartPlugin.h"

#include "UartChannel.h"

#include <QSerialPortInfo>

namespace spotty {

namespace {

/// \brief Приставка идентификаторов этого плагина.
constexpr auto kIdPrefix = "uart:";

/**
 * \brief Построить устойчивый идентификатор порта.
 *
 * Опора на свойства, зашитые в железо, а не на имя устройства. Узел `/dev` у
 * USB-переходника переназначается при каждом переподключении: `/dev/cu.usbserial-1420`
 * легко становится `/dev/cu.usbserial-1430`. Идентификатор, выведенный из имени, при этом
 * меняется, ядро считает устройство новым и сбрасывает скорость, псевдоним и режим, а
 * автоматическое переоткрытие не срабатывает.
 *
 * К имени опускаемся только когда свойств нет: встроенный UART, виртуальный порт от
 * socat, некоторые переходники без серийного номера.
 */
QString buildStableId(const QSerialPortInfo &info)
{
    if (info.hasVendorIdentifier() && info.hasProductIdentifier()) {
        const QString serial = info.serialNumber();
        return QStringLiteral("%1%2:%3:%4")
            .arg(QLatin1String(kIdPrefix))
            .arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0'))
            .arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))
            // Переходники без серийного номера различаются хотя бы именем порта: два
            // одинаковых CH340 иначе получили бы один идентификатор на двоих.
            .arg(serial.isEmpty() ? info.portName() : serial);
    }
    return QLatin1String(kIdPrefix) + info.portName();
}

} // namespace

QList<InterfaceDescriptor> UartPlugin::enumerate() const
{
    QList<InterfaceDescriptor> result;

    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    result.reserve(ports.size());

    for (const QSerialPortInfo &info : ports) {
        InterfaceDescriptor descriptor;
        descriptor.id = buildStableId(info);
        descriptor.systemName = info.portName();
        descriptor.description = info.description();

        if (!info.manufacturer().isEmpty())
            descriptor.extra.insert(QStringLiteral("manufacturer"), info.manufacturer());
        if (!info.serialNumber().isEmpty())
            descriptor.extra.insert(QStringLiteral("serialNumber"), info.serialNumber());
        if (info.hasVendorIdentifier()) {
            descriptor.extra.insert(
                QStringLiteral("vendorId"),
                QStringLiteral("0x%1").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0')));
        }
        if (info.hasProductIdentifier()) {
            descriptor.extra.insert(
                QStringLiteral("productId"),
                QStringLiteral("0x%1").arg(info.productIdentifier(), 4, 16, QLatin1Char('0')));
        }
        descriptor.extra.insert(QStringLiteral("systemLocation"), info.systemLocation());

        result.append(descriptor);
    }

    return result;
}

SettingsSchema UartPlugin::settingsSchema() const
{
    SettingsSchema schema;

    const QString portGroup = tr("Port");
    const QString flowGroup = tr("Flow control");
    const QString linesGroup = tr("Control lines");

    schema.add(SettingsField{
        .key = QStringLiteral("baudRate"),
        .label = tr("Baud rate"),
        .group = portGroup,
        .type = SettingsField::Choice,
        .defaultValue = 115200,
        .options = {{QStringLiteral("1200"), 1200},
                    {QStringLiteral("2400"), 2400},
                    {QStringLiteral("4800"), 4800},
                    {QStringLiteral("9600"), 9600},
                    {QStringLiteral("19200"), 19200},
                    {QStringLiteral("38400"), 38400},
                    {QStringLiteral("57600"), 57600},
                    {QStringLiteral("115200"), 115200},
                    {QStringLiteral("230400"), 230400},
                    {QStringLiteral("460800"), 460800},
                    {QStringLiteral("921600"), 921600},
                    {QStringLiteral("1500000"), 1500000},
                    {QStringLiteral("3000000"), 3000000}},
        // Перечисление принципиально неполно: устройства используют и нестандартные
        // скорости, а драйверы их часто позволяют.
        .editable = true,
        .suffix = tr("bps"),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("dataBits"),
        .label = tr("Data bits"),
        .group = portGroup,
        .type = SettingsField::Choice,
        .defaultValue = 8,
        .options = {{QStringLiteral("5"), 5},
                    {QStringLiteral("6"), 6},
                    {QStringLiteral("7"), 7},
                    {QStringLiteral("8"), 8}},
    });

    schema.add(SettingsField{
        .key = QStringLiteral("parity"),
        .label = tr("Parity"),
        .group = portGroup,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("N"),
        .options = {{tr("None"), QStringLiteral("N")},
                    {tr("Even"), QStringLiteral("E")},
                    {tr("Odd"), QStringLiteral("O")},
                    {tr("Mark"), QStringLiteral("M")},
                    {tr("Space"), QStringLiteral("S")}},
    });

    schema.add(SettingsField{
        .key = QStringLiteral("stopBits"),
        .label = tr("Stop bits"),
        .group = portGroup,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("1"),
        .options = {{QStringLiteral("1"), QStringLiteral("1")},
                    {QStringLiteral("1.5"), QStringLiteral("1.5")},
                    {QStringLiteral("2"), QStringLiteral("2")}},
    });

    schema.add(SettingsField{
        .key = QStringLiteral("flowControl"),
        .label = tr("Flow control"),
        .group = flowGroup,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("none"),
        .options = {{tr("None"), QStringLiteral("none")},
                    {tr("Hardware (RTS/CTS)"), QStringLiteral("hardware")},
                    {tr("Software (XON/XOFF)"), QStringLiteral("software")}},
    });

    schema.add(SettingsField{
        .key = QStringLiteral("dtrOnOpen"),
        .label = tr("Assert DTR on open"),
        .group = linesGroup,
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("On many boards DTR is wired to reset - clear it to avoid rebooting "
                   "the device when the port opens."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("rtsOnOpen"),
        .label = tr("Assert RTS on open"),
        .group = linesGroup,
        .type = SettingsField::Toggle,
        .defaultValue = true,
    });

    return schema;
}

QString UartPlugin::settingsSummary(const QVariantMap &settings) const
{
    if (settings.isEmpty())
        return {};

    return QStringLiteral("%1 %2-%3-%4")
        .arg(settings.value(QStringLiteral("baudRate")).toInt())
        .arg(settings.value(QStringLiteral("dataBits")).toInt())
        .arg(settings.value(QStringLiteral("parity")).toString())
        .arg(settings.value(QStringLiteral("stopBits")).toString());
}

IInterfaceChannel *UartPlugin::createChannel(const InterfaceDescriptor &descriptor)
{
    if (!descriptor.id.startsWith(QLatin1String(kIdPrefix)))
        return nullptr;

    // Открывать порт нужно по системному имени, а не по идентификатору: идентификатор
    // намеренно от имени не зависит, ради переживания переподключений.
    if (descriptor.systemName.isEmpty())
        return nullptr;

    return new UartChannel(descriptor.systemName);
}

} // namespace spotty
