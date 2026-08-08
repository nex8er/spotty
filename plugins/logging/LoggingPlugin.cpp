/**
 * \file LoggingPlugin.cpp
 * \brief Реализация spotty::LoggingPlugin.
 */
#include "LoggingPlugin.h"

#include "LoggingPanel.h"

#include <spotty/ui/MdiCodepoints.h>

namespace spotty {

QList<PanelDescriptor> LoggingPlugin::panels() const
{
    return {PanelDescriptor{
        .id = QStringLiteral("logging"),
        .title = tr("Logging"),
        .glyph = mdi::RecordCircleOutline,
        .placement = PanelPlacement::Rail,
        .order = 200,
    }};
}

QWidget *LoggingPlugin::createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
{
    if (panelId != QLatin1String("logging"))
        return nullptr;
    return new LoggingPanel(host, parent);
}

SettingsSchema LoggingPlugin::settingsSchema() const
{
    SettingsSchema schema;

    schema.add(SettingsField{
        .key = QStringLiteral("directory"),
        .label = tr("Log directory"),
        .group = tr("Files"),
        .type = SettingsField::Text,
        // Умолчание пустое намеренно: подставить сюда абсолютный путь значило бы записать
        // его в настройки, и переносной комплект унёс бы путь чужой машины. Пустое поле
        // разворачивается в каталог по умолчанию при чтении.
        .defaultValue = QString(),
        .hint = tr("Leave empty to use the default location."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("fileNameTemplate"),
        .label = tr("File name"),
        .group = tr("Files"),
        .type = SettingsField::Text,
        .defaultValue = QStringLiteral("{alias}_{date}_{time}"),
        .hint = tr("Placeholders: {interface}, {alias}, {date}, {time}."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("filterAnsi"),
        .label = tr("Strip ANSI escape sequences"),
        .group = tr("Contents"),
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("Colour codes make the file hard to read outside a terminal."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("includeTx"),
        .label = tr("Include sent data"),
        .group = tr("Contents"),
        .type = SettingsField::Toggle,
        .defaultValue = true,
    });

    schema.add(SettingsField{
        .key = QStringLiteral("autoStart"),
        .label = tr("Start recording when the interface opens"),
        .group = tr("Contents"),
        .type = SettingsField::Toggle,
        .defaultValue = false,
    });

    return schema;
}

} // namespace spotty
