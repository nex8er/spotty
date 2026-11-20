/**
 * \file NodesPlugin.h
 * \brief Панель узлов транспорта без зависимости от реализации CAN.
 */
#pragma once
#include <spotty/ui/IPanelPlugin.h>

namespace spotty {
class NodesPlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_PANEL_PLUGIN_IID FILE "nodes.json")
    Q_INTERFACES(spotty::IPanelPlugin)
public:
    QString pluginId() const override { return QStringLiteral("nodes"); }
    QString displayName() const override { return tr("Network nodes"); }
    QList<PanelDescriptor> panels() const override;
    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override;
};
} // namespace spotty
