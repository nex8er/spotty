/**
 * \file JsonGenPlugin.cpp
 * \brief Реализация spotty::JsonGenPlugin.
 */
#include "JsonGenPlugin.h"

#include "JsonGenChannel.h"

namespace spotty {

QList<InterfaceDescriptor> JsonGenPlugin::enumerate() const
{
    InterfaceDescriptor device;
    device.id = QStringLiteral("jsongen:0");
    device.systemName = QStringLiteral("jsongen0");
    device.description = tr("Virtual JSON telemetry source");
    return {device};
}

SettingsSchema JsonGenPlugin::settingsSchema() const
{
    SettingsSchema schema;

    schema.add(SettingsField{
        .key = QStringLiteral("shape"),
        .label = tr("Document shape"),
        .group = tr("Shape"),
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("flat"),
        .options = {
            {tr("Flat object"), QStringLiteral("flat")},
            {tr("Nested object"), QStringLiteral("nested")},
            {tr("Array of objects"), QStringLiteral("array")},
            {tr("Mixed (rotates through all three)"), QStringLiteral("mixed")},
        },
        .hint = tr("\"Mixed\" alternates shapes from document to document - the case a "
                   "parser is most likely to get wrong."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("fieldCount"),
        .label = tr("Fields per object"),
        .group = tr("Shape"),
        .type = SettingsField::Integer,
        .defaultValue = 6,
        .minimum = 1,
        .maximum = 64,
    });

    schema.add(SettingsField{
        .key = QStringLiteral("depth"),
        .label = tr("Nesting depth"),
        .group = tr("Shape"),
        .type = SettingsField::Integer,
        .defaultValue = 3,
        .minimum = 1,
        .maximum = 8,
        .hint = tr("Only used by the nested and mixed shapes."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("arraySize"),
        .label = tr("Objects per array"),
        .group = tr("Shape"),
        .type = SettingsField::Integer,
        .defaultValue = 3,
        .minimum = 1,
        .maximum = 64,
    });

    schema.add(SettingsField{
        .key = QStringLiteral("uuidIds"),
        .label = tr("Use random ids"),
        .group = tr("Shape"),
        .type = SettingsField::Toggle,
        .defaultValue = false,
        .hint = tr("Every array element gets a brand new id, so the tree grows without "
                   "bound - this is what a node limit is for."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("intervalMs"),
        .label = tr("Document interval"),
        .group = tr("Rate"),
        .type = SettingsField::Integer,
        .defaultValue = 200,
        .minimum = 1,
        .maximum = 60000,
        .suffix = tr("ms"),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("mixedRates"),
        .label = tr("Different rates per field"),
        .group = tr("Rate"),
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("Some fields come with every document, some every 5th, some every 25th. "
                   "A field that is not due is left out of the document entirely - without "
                   "that there is nothing for a rate column to show."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("corruptPercent"),
        .label = tr("Corrupt documents"),
        .group = tr("Noise"),
        .type = SettingsField::Integer,
        .defaultValue = 0,
        .minimum = 0,
        .maximum = 100,
        .suffix = QStringLiteral("%"),
        .hint = tr("Truncated tails, missing braces, stray commas - a parser must skip "
                   "these instead of breaking or littering its output."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("logLines"),
        .label = tr("Emit plain log lines"),
        .group = tr("Noise"),
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("Ordinary text mixed into the telemetry, the way real firmware does it."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("layout"),
        .label = tr("Layout"),
        .group = tr("Format"),
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("compact"),
        .options = {
            {tr("One document per line"), QStringLiteral("compact")},
            {tr("Pretty-printed over several lines"), QStringLiteral("pretty")},
            {tr("Alternate between the two"), QStringLiteral("alternate")},
        },
    });

    schema.add(SettingsField{
        .key = QStringLiteral("splitPackets"),
        .label = tr("Split documents into two packets"),
        .group = tr("Format"),
        .type = SettingsField::Toggle,
        .defaultValue = false,
        .hint = tr("Breaks the stream mid-line, the way a polled source does. A reader must "
                   "wait for the line to finish instead of parsing half of it."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("driftMode"),
        .label = tr("Structure drift"),
        .group = tr("Drift"),
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("off"),
        .options = {
            {tr("Off"), QStringLiteral("off")},
            {tr("Keep adding new keys"), QStringLiteral("grow")},
            {tr("Keys come and go"), QStringLiteral("cycle")},
        },
        .hint = tr("Checks that a tree grows with the stream and shows what stopped coming."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("driftEveryDocs"),
        .label = tr("Drift every"),
        .group = tr("Drift"),
        .type = SettingsField::Integer,
        .defaultValue = 25,
        .minimum = 1,
        .maximum = 10000,
        .suffix = tr("documents"),
    });

    return schema;
}

QString JsonGenPlugin::settingsSummary(const QVariantMap &settings) const
{
    return QStringLiteral("%1 · %2 ms")
        .arg(settings.value(QStringLiteral("shape")).toString(),
             settings.value(QStringLiteral("intervalMs")).toString());
}

IInterfaceChannel *JsonGenPlugin::createChannel(const InterfaceDescriptor &descriptor)
{
    if (!descriptor.id.startsWith(QLatin1String("jsongen:")))
        return nullptr;
    return new JsonGenChannel();
}

} // namespace spotty
