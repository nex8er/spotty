/**
 * \file LoopbackPlugin.cpp
 * \brief Реализация spotty::LoopbackPlugin.
 */
#include "LoopbackPlugin.h"

#include "LoopbackChannel.h"

namespace spotty {

QList<InterfaceDescriptor> LoopbackPlugin::enumerate() const
{
    // Ровно два устройства: так проверяются и список интерфейсов, и переименование, и
    // раздельные настройки для каждого — всё это без единого подключённого прибора.
    //
    // Идентификаторы постоянны, потому что виртуальные устройства никуда не переезжают.
    // Для настоящего железа так делать нельзя: см. предупреждение у
    // spotty::InterfaceDescriptor::id.
    InterfaceDescriptor echo;
    echo.id = QStringLiteral("loopback:echo");
    echo.systemName = QStringLiteral("loopback0");
    echo.description = tr("Virtual echo channel");

    InterfaceDescriptor chatter;
    chatter.id = QStringLiteral("loopback:chatter");
    chatter.systemName = QStringLiteral("loopback1");
    chatter.description = tr("Virtual data source");

    return {echo, chatter};
}

SettingsSchema LoopbackPlugin::settingsSchema() const
{
    SettingsSchema schema;

    schema.add(SettingsField{
        .key = QStringLiteral("mode"),
        .label = tr("Mode"),
        .group = tr("Behaviour"),
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("echo"),
        .options = {{tr("Echo what is sent"), QStringLiteral("echo")},
                    {tr("Emit generated data"), QStringLiteral("chatter")},
                    {tr("Silent"), QStringLiteral("silent")}},
    });

    schema.add(SettingsField{
        .key = QStringLiteral("echoDelayMs"),
        .label = tr("Echo delay"),
        .group = tr("Behaviour"),
        .type = SettingsField::Integer,
        .defaultValue = 0,
        .minimum = 0,
        .maximum = 5000,
        .suffix = tr("ms"),
        .hint = tr("Simulates a device that takes time to answer."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("chatterIntervalMs"),
        .label = tr("Emit interval"),
        .group = tr("Behaviour"),
        .type = SettingsField::Integer,
        .defaultValue = 500,
        .minimum = 1,
        .maximum = 60000,
        .suffix = tr("ms"),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("ansiColors"),
        .label = tr("Include ANSI colour codes"),
        .group = tr("Behaviour"),
        .type = SettingsField::Toggle,
        .defaultValue = true,
    });

    return schema;
}

IInterfaceChannel *LoopbackPlugin::createChannel(const InterfaceDescriptor &descriptor)
{
    if (!descriptor.id.startsWith(QLatin1String("loopback:")))
        return nullptr;
    return new LoopbackChannel;
}

} // namespace spotty
