/**
 * \file SignalGenPlugin.cpp
 * \brief Реализация spotty::SignalGenPlugin.
 */
#include "SignalGenPlugin.h"

#include "SignalGenChannel.h"

namespace spotty {

QList<InterfaceDescriptor> SignalGenPlugin::enumerate() const
{
    InterfaceDescriptor device;
    device.id = QStringLiteral("signalgen:0");
    device.systemName = QStringLiteral("signalgen0");
    device.description = tr("Virtual math signal source");
    return {device};
}

SettingsSchema SignalGenPlugin::settingsSchema() const
{
    SettingsSchema schema;

    schema.add(SettingsField{
        .key = QStringLiteral("waveform"),
        .label = tr("Waveform"),
        .group = tr("Waveform"),
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("sine"),
        .options = {
            {tr("Sine"), QStringLiteral("sine")},
            {tr("Cosine"), QStringLiteral("cosine")},
            {tr("Square"), QStringLiteral("square")},
            {tr("Triangle"), QStringLiteral("triangle")},
            {tr("Sawtooth"), QStringLiteral("sawtooth")},
            {tr("Noise"), QStringLiteral("noise")},
            {tr("Chirp (rising frequency)"), QStringLiteral("chirp")},
            {tr("Damped sine (retriggered)"), QStringLiteral("decay")},
            {tr("Pulse train"), QStringLiteral("pulse")},
            {tr("Staircase"), QStringLiteral("steps")},
            {tr("All waveforms (6 columns)"), QStringLiteral("all")},
            {tr("Growing column count"), QStringLiteral("growing")},
        },
        .hint = tr("\"All waveforms\" and \"Growing column count\" emit several columns at "
                   "once - handy for testing the chart panel's series table, its colours, "
                   "and how it reacts to a series appearing mid-stream."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("periodMs"),
        .label = tr("Period"),
        .group = tr("Waveform"),
        .type = SettingsField::Integer,
        .defaultValue = 2000,
        .minimum = 20,
        .maximum = 600000,
        .suffix = tr("ms"),
        .hint = tr("Length of one cycle. Chirp and the retriggered sine use it as the "
                   "starting period of a longer, repeating pattern."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("amplitude"),
        .label = tr("Amplitude"),
        .group = tr("Waveform"),
        .type = SettingsField::Integer,
        .defaultValue = 100,
        .minimum = 1,
        .maximum = 1000000,
    });

    schema.add(SettingsField{
        .key = QStringLiteral("offset"),
        .label = tr("Offset"),
        .group = tr("Waveform"),
        .type = SettingsField::Integer,
        .defaultValue = 0,
        .minimum = -1000000,
        .maximum = 1000000,
        .hint = tr("Added to every value; shifts the curve up or down."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("noisePercent"),
        .label = tr("Noise"),
        .group = tr("Waveform"),
        .type = SettingsField::Integer,
        .defaultValue = 0,
        .minimum = 0,
        .maximum = 100,
        .suffix = QStringLiteral("%"),
        .hint = tr("Random jitter layered on top of the waveform, as a percentage of the "
                   "amplitude."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("sampleIntervalMs"),
        .label = tr("Sample interval"),
        .group = tr("Output"),
        .type = SettingsField::Integer,
        .defaultValue = 100,
        .minimum = 1,
        .maximum = 60000,
        .suffix = tr("ms"),
        .hint = tr("How often a new line is emitted."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("includeAxis"),
        .label = tr("Prepend time column"),
        .group = tr("Output"),
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("First column holds seconds elapsed since the channel opened - a steady "
                   "reference axis, useful as a custom X axis in the chart panel."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("includeMarkers"),
        .label = tr("Emit status text lines"),
        .group = tr("Output"),
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("Occasionally sends a non-numeric line, like a device mixing log messages "
                   "into telemetry - checks that the chart skips it instead of breaking."),
    });

    return schema;
}

QString SignalGenPlugin::settingsSummary(const QVariantMap &settings) const
{
    return QStringLiteral("%1 · %2 ms")
        .arg(settings.value(QStringLiteral("waveform")).toString(),
             settings.value(QStringLiteral("periodMs")).toString());
}

IInterfaceChannel *SignalGenPlugin::createChannel(const InterfaceDescriptor &descriptor)
{
    if (!descriptor.id.startsWith(QLatin1String("signalgen:")))
        return nullptr;
    return new SignalGenChannel();
}

} // namespace spotty
