/**
 * \file FileSendPlugin.cpp
 * \brief Реализация spotty::FileSendPlugin.
 */
#include "FileSendPlugin.h"

#include "FileSendPanel.h"

#include <spotty/ui/MdiCodepoints.h>

namespace spotty {

QList<PanelDescriptor> FileSendPlugin::panels() const
{
    return {PanelDescriptor{
        .id = QStringLiteral("filesend"),
        .title = tr("Send file"),
        .glyph = mdi::FileUpload,
        .placement = PanelPlacement::Rail,
        .order = 600,
    }};
}

QWidget *FileSendPlugin::createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
{
    if (panelId != QLatin1String("filesend"))
        return nullptr;
    return new FileSendPanel(host, parent);
}

} // namespace spotty
