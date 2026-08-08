/**
 * \file GeneratorPlugin.h
 * \brief Панельный плагин генератора тестовых посылок.
 */
#pragma once

#include <spotty/ui/IPanelPlugin.h>

namespace spotty {

/**
 * \class GeneratorPlugin
 * \brief Объявляет панель генератора.
 *
 * Пока вкомпилирован в приложение и регистрируется через
 * spotty::PanelPluginRegistry::addBuiltin(). Отдельным загружаемым модулем станет без
 * правок в этом файле — от него требуется только линковка со Spotty::UiApi и
 * Q_PLUGIN_METADATA.
 */
class GeneratorPlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_PANEL_PLUGIN_IID FILE "generator.json")
    Q_INTERFACES(spotty::IPanelPlugin)

public:
    QString pluginId() const override { return QStringLiteral("generator"); }
    QString displayName() const override { return tr("Generator"); }

    QList<PanelDescriptor> panels() const override;
    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override;
};

} // namespace spotty
