/**
 * \file SearchPlugin.h
 * \brief Панельный плагин поиска и подсветки.
 */
#pragma once

#include <spotty/ui/IPanelPlugin.h>

namespace spotty {

/**
 * \class SearchPlugin
 * \brief Объявляет панель поиска.
 */
class SearchPlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_PANEL_PLUGIN_IID FILE "search.json")
    Q_INTERFACES(spotty::IPanelPlugin)

public:
    QString pluginId() const override { return QStringLiteral("search"); }
    QString displayName() const override { return tr("Search"); }

    QList<PanelDescriptor> panels() const override;
    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override;
};

} // namespace spotty
