/**
 * \file StreamServerPlugin.cpp
 * \brief Реализация spotty::StreamServerPlugin.
 */
#include "StreamServerPlugin.h"

#include "StreamServerPanel.h"

#include <spotty/ui/MdiCodepoints.h>

namespace spotty {

QList<PanelDescriptor> StreamServerPlugin::panels() const
{
    return {PanelDescriptor{
        .id = QStringLiteral("streamserver"),
        .title = tr("Stream server"),
        .glyph = mdi::Connection,
        .placement = PanelPlacement::Rail,
        .order = 700,
    }};
}

QWidget *StreamServerPlugin::createPanel(const QString &panelId, IPanelHost *host,
                                         QWidget *parent)
{
    if (panelId != QLatin1String("streamserver"))
        return nullptr;
    return new StreamServerPanel(host, parent);
}

} // namespace spotty
