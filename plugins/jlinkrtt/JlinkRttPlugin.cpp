/**
 * \file JlinkRttPlugin.cpp
 * \brief Реализация spotty::JlinkRttPlugin.
 */
#include "JlinkRttPlugin.h"

#include "JlinkArmLibrary.h"
#include "JlinkDeviceDatabase.h"
#include "JlinkRttChannel.h"

namespace spotty {

namespace {

constexpr auto kIdPrefix = "jlinkrtt:";

/// \brief Сколько номеров буфера предлагать на каждый найденный зонд.
///
/// Реальное число буферов, заведённых прошивкой, неизвестно без подключения (см.
/// JlinkRttPlugin). Восемь с запасом покрывает обычные схемы использования; лишнее
/// скрывается через уже существующий механизм InterfaceEntry::hidden.
constexpr int kChannelCount = 8;

} // namespace

QList<InterfaceDescriptor> JlinkRttPlugin::enumerate() const
{
    QList<InterfaceDescriptor> result;

    const QList<JlinkArmLibrary::ProbeInfo> probes = JlinkArmLibrary::instance().enumerateProbes();
    result.reserve(probes.size() * kChannelCount);

    for (const JlinkArmLibrary::ProbeInfo &probe : probes) {
        for (int channel = 0; channel < kChannelCount; ++channel) {
            InterfaceDescriptor descriptor;
            descriptor.id = QStringLiteral("%1%2:%3")
                                .arg(QLatin1String(kIdPrefix))
                                .arg(probe.serialNumber)
                                .arg(channel);
            descriptor.systemName = QStringLiteral("RTT%1").arg(channel);
            descriptor.description = probe.product.isEmpty() ? tr("J-Link") : probe.product;
            descriptor.serialNumber = QString::number(probe.serialNumber);
            // Канал 0 — почти всегда единственный, что реально используется («Terminal»);
            // остальные семь — запас на случай, если прошивка заводит больше. Прячем их,
            // чтобы при первом подключении зонда список не выглядел замусоренным.
            descriptor.hiddenByDefault = (channel != 0);
            result.append(descriptor);
        }
    }

    return result;
}

SettingsSchema JlinkRttPlugin::settingsSchema() const
{
    SettingsSchema schema;
    const QString group = tr("Connection");

    schema.add(SettingsField{
        .key = QStringLiteral("targetInterface"),
        .label = tr("Target interface"),
        .group = group,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("swd"),
        .options = {{tr("SWD"), QStringLiteral("swd")}, {tr("JTAG"), QStringLiteral("jtag")}},
    });

    schema.add(SettingsField{
        .key = QStringLiteral("speedKhz"),
        .label = tr("Speed"),
        .group = group,
        .type = SettingsField::Choice,
        .defaultValue = 4000,
        .options = {{QStringLiteral("100"), 100},
                    {QStringLiteral("500"), 500},
                    {QStringLiteral("1000"), 1000},
                    {QStringLiteral("2000"), 2000},
                    {QStringLiteral("4000"), 4000},
                    {QStringLiteral("8000"), 8000},
                    {QStringLiteral("15000"), 15000},
                    {QStringLiteral("25000"), 25000},
                    {QStringLiteral("50000"), 50000}},
        .editable = true,
        .suffix = tr("kHz"),
    });

    QList<SettingsOption> deviceOptions;
    const QStringList deviceNames = JlinkDeviceDatabase::instance().deviceNames();
    deviceOptions.reserve(deviceNames.size());
    for (const QString &name : deviceNames)
        deviceOptions.append(SettingsOption{name, name});

    schema.add(SettingsField{
        .key = QStringLiteral("targetDevice"),
        .label = tr("Target device"),
        .group = group,
        .type = SettingsField::Choice,
        .defaultValue = QString(),
        .options = deviceOptions,
        .editable = true,
        .required = true,
        .hint = deviceOptions.isEmpty()
                   ? tr("Exact SEGGER device name, e.g. \"NRF52832_XXAA\". Required to "
                        "connect. The J-Link device database was not found, so there are "
                        "no suggestions — install SEGGER J-Link software for autocomplete.")
                   : tr("Exact SEGGER device name, e.g. \"NRF52832_XXAA\" — start typing to "
                        "search. Required to connect."),
    });

    return schema;
}

QString JlinkRttPlugin::settingsSummary(const QVariantMap &settings) const
{
    const QString interfaceType =
        settings.value(QStringLiteral("targetInterface")).toString().toUpper();
    const int speed = settings.value(QStringLiteral("speedKhz")).toInt();
    return tr("%1 %2 kHz").arg(interfaceType).arg(speed);
}

IInterfaceChannel *JlinkRttPlugin::createChannel(const InterfaceDescriptor &descriptor)
{
    if (!descriptor.id.startsWith(QLatin1String(kIdPrefix)))
        return nullptr;

    const QStringList parts = descriptor.id.mid(qstrlen(kIdPrefix)).split(QLatin1Char(':'));
    if (parts.size() != 2)
        return nullptr;

    bool serialOk = false;
    bool channelOk = false;
    const quint32 serialNumber = parts.at(0).toUInt(&serialOk);
    const int channel = parts.at(1).toInt(&channelOk);
    if (!serialOk || !channelOk)
        return nullptr;

    return new JlinkRttChannel(serialNumber, channel);
}

} // namespace spotty
