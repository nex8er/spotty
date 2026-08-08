/**
 * \file SearchPlugin.cpp
 * \brief Реализация spotty::SearchPlugin.
 */
#include "SearchPlugin.h"

#include "SearchPanel.h"

#include <spotty/ui/MdiCodepoints.h>

namespace spotty {

QList<PanelDescriptor> SearchPlugin::panels() const
{
    return {PanelDescriptor{
        .id = QStringLiteral("search"),
        .title = tr("Search"),
        .glyph = mdi::Magnify,
        .placement = PanelPlacement::Rail,
        .order = 300,
    }};
}

QWidget *SearchPlugin::createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
{
    if (panelId != QLatin1String("search"))
        return nullptr;
    return new SearchPanel(host, parent);
}

} // namespace spotty
