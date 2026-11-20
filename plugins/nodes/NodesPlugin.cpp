/**
 * \file NodesPlugin.cpp
 * \brief Регистрация вкладки узлов на общей рейке панелей.
 */
#include "NodesPlugin.h"
#include "NodesPanel.h"
#include <spotty/ui/MdiCodepoints.h>

namespace spotty {
QList<PanelDescriptor> NodesPlugin::panels() const
{
    return {{.id = QStringLiteral("nodes"), .title = tr("Nodes"),
             .glyph = mdi::Lan, .placement = PanelPlacement::Rail, .order = 800}};
}

QWidget *NodesPlugin::createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
{
    return panelId == QLatin1String("nodes") ? new NodesPanel(host, parent) : nullptr;
}
} // namespace spotty
