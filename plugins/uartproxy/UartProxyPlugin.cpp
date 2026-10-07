/**
 * \file UartProxyPlugin.cpp
 * \brief Реализация spotty::UartProxyPlugin.
 */
#include "UartProxyPlugin.h"

#include "UartProxyChannel.h"
#include "UartProxyPorts.h"
#include "UartProxySettings.h"

namespace spotty {

using namespace uartproxy;

namespace {

constexpr auto kDeviceId = "uartproxy:tap";

} // namespace

QList<InterfaceDescriptor> UartProxyPlugin::enumerate() const
{
    // Устройство одно и виртуальное, поэтому и идентификатор постоянный. Для настоящих
    // портов так делать нельзя (см. предупреждение у spotty::InterfaceDescriptor::id), но
    // здесь реальный порт выбирается настройкой и хранится устойчивым идентификатором.
    InterfaceDescriptor tap;
    tap.id = QLatin1String(kDeviceId);
    tap.systemName = QStringLiteral("uartproxy");
    tap.description = tr("Intercept a port opened by another program");
    return {tap};
}

SettingsSchema UartProxyPlugin::settingsSchema() const
{
    SettingsSchema schema;

    const QString realGroup = tr("Real port");
    const QString virtualGroup = tr("Virtual port");

    schema.add(SettingsField{
        .key = QLatin1String(kRealPort),
        .label = tr("Port with the device"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = QString(),
        .options = portOptions(/*byName=*/false),
        .live = true,
        .editable = true,
        .required = true,
        .hint = tr("Spotty opens this port itself, so close it in the other program."),
    });

    schema.add(SettingsField{
        .key = QLatin1String(kBaudRate),
        .label = tr("Baud rate"),
        .group = realGroup,
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
        .editable = true,
        .hint = tr("Must match what the other program uses: a virtual port cannot report it."),
    });

    schema.add(SettingsField{
        .key = QLatin1String(kDataBits),
        .label = tr("Data bits"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = 8,
        .options = {{QStringLiteral("5"), 5},
                    {QStringLiteral("6"), 6},
                    {QStringLiteral("7"), 7},
                    {QStringLiteral("8"), 8}},
    });

    schema.add(SettingsField{
        .key = QLatin1String(kParity),
        .label = tr("Parity"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("N"),
        .options = {{tr("None"), QStringLiteral("N")},
                    {tr("Even"), QStringLiteral("E")},
                    {tr("Odd"), QStringLiteral("O")},
                    {tr("Mark"), QStringLiteral("M")},
                    {tr("Space"), QStringLiteral("S")}},
    });

    schema.add(SettingsField{
        .key = QLatin1String(kStopBits),
        .label = tr("Stop bits"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("1"),
        .options = {{QStringLiteral("1"), QStringLiteral("1")},
                    {QStringLiteral("1.5"), QStringLiteral("1.5")},
                    {QStringLiteral("2"), QStringLiteral("2")}},
    });

    schema.add(SettingsField{
        .key = QLatin1String(kFlowControl),
        .label = tr("Flow control"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("none"),
        .options = {{tr("None"), QStringLiteral("none")},
                    {tr("Hardware (RTS/CTS)"), QStringLiteral("hardware")},
                    {tr("Software (XON/XOFF)"), QStringLiteral("software")}},
    });

    schema.add(SettingsField{
        .key = QLatin1String(kDtrOnOpen),
        .label = tr("Assert DTR on open"),
        .group = realGroup,
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("On many boards DTR is wired to reset - clear it to avoid rebooting "
                   "the device when the port opens."),
    });

    schema.add(SettingsField{
        .key = QLatin1String(kRtsOnOpen),
        .label = tr("Assert RTS on open"),
        .group = realGroup,
        .type = SettingsField::Toggle,
        .defaultValue = true,
    });

#ifdef Q_OS_UNIX
    schema.add(SettingsField{
        .key = QLatin1String(kLinkPath),
        .label = tr("Link path"),
        .group = virtualGroup,
        .type = SettingsField::Text,
        .defaultValue = QLatin1String(kDefaultLinkPath),
        .hint = tr("Spotty creates a virtual port and puts a link with this fixed path to it: "
                   "enter the path in the other program. Leave empty to use the port's own "
                   "path, shown after opening - it changes on every run."),
    });
#endif

    schema.add(SettingsField{
        .key = QLatin1String(kVirtualPort),
        .label = tr("Existing virtual port"),
        .group = virtualGroup,
        .type = SettingsField::Choice,
        .defaultValue = QString(),
        .options = {},
        .live = true,
        .editable = true,
#ifdef Q_OS_UNIX
        .hint = tr("Optional: use one end of a ready pair (for example made by socat) "
                   "instead of creating a port."),
#else
        .required = true,
        .hint = tr("One end of a virtual port pair (for example com0com): the other program "
                   "opens the other end."),
#endif
    });

    return schema;
}

QString UartProxyPlugin::settingsSummary(const QVariantMap &settings) const
{
    if (settings.isEmpty())
        return {};

    QString summary = QStringLiteral("%1 %2-%3-%4")
                          .arg(settings.value(QLatin1String(kBaudRate)).toInt())
                          .arg(settings.value(QLatin1String(kDataBits)).toInt())
                          .arg(settings.value(QLatin1String(kParity)).toString())
                          .arg(settings.value(QLatin1String(kStopBits)).toString());

    // То, что нужно ввести в чужой программе, — самое важное сведение об этом
    // интерфейсе, и спрятать его в диалоге значит заставлять искать каждый раз.
    QString where = settings.value(QLatin1String(kVirtualPort)).toString().trimmed();
    if (where.isEmpty())
        where = settings.value(QLatin1String(kLinkPath)).toString().trimmed();
    if (!where.isEmpty())
        summary += QStringLiteral(" → ") + where;
    return summary;
}

QList<SettingsOption> UartProxyPlugin::liveOptions(const InterfaceDescriptor &descriptor,
                                                   const QString &key,
                                                   const QVariantMap &settings)
{
    Q_UNUSED(descriptor);
    Q_UNUSED(settings);

    // Реальный порт хранится устойчивым идентификатором, виртуальный — именем: он не
    // обязан быть USB-устройством, и свойств для идентификатора у него нет.
    if (key == QLatin1String(kRealPort))
        return portOptions(/*byName=*/false);
    if (key == QLatin1String(kVirtualPort))
        return portOptions(/*byName=*/true);
    return {};
}

IInterfaceChannel *UartProxyPlugin::createChannel(const InterfaceDescriptor &descriptor)
{
    if (descriptor.id != QLatin1String(kDeviceId))
        return nullptr;
    return new UartProxyChannel;
}

} // namespace spotty
