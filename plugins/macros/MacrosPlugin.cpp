/**
 * \file MacrosPlugin.cpp
 * \brief Реализация spotty::MacrosPlugin.
 */
#include "MacrosPlugin.h"

#include "MacrosPanel.h"

#include <spotty/ui/MdiCodepoints.h>

namespace spotty {

QList<PanelDescriptor> MacrosPlugin::panels() const
{
    return {PanelDescriptor{
        .id = QStringLiteral("macros"),
        .title = tr("Macros"),
        .glyph = mdi::Flash,
        .placement = PanelPlacement::Rail,
        .order = 100,
    }};
}

QWidget *MacrosPlugin::createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
{
    if (panelId != QLatin1String("macros"))
        return nullptr;
    return new MacrosPanel(host, parent);
}

} // namespace spotty
