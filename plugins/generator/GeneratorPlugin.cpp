/**
 * \file GeneratorPlugin.cpp
 * \brief Реализация spotty::GeneratorPlugin.
 */
#include "GeneratorPlugin.h"

#include "GeneratorPanel.h"

#include <spotty/ui/MdiCodepoints.h>

namespace spotty {

QList<PanelDescriptor> GeneratorPlugin::panels() const
{
    return {PanelDescriptor{
        .id = QStringLiteral("generator"),
        .title = tr("Generator"),
        .glyph = mdi::ShuffleVariant,
        .placement = PanelPlacement::Rail,
        // Порядок сохраняет прежний вид рейки: макросы, журнал, поиск, генератор.
        .order = 400,
    }};
}

QWidget *GeneratorPlugin::createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
{
    if (panelId != QLatin1String("generator"))
        return nullptr;
    return new GeneratorPanel(host, parent);
}

} // namespace spotty
