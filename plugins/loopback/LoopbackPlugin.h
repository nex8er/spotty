/**
 * \file LoopbackPlugin.h
 * \brief Плагин виртуальных каналов.
 */
#pragma once

#include <spotty/api/IInterfacePlugin.h>

#include <QObject>

namespace spotty {

/**
 * \class LoopbackPlugin
 * \brief Предоставляет виртуальные каналы, не требующие оборудования.
 *
 * Заодно служит образцом минимального плагина: пять переопределённых методов и один вызов
 * spotty_add_plugin() в CMakeLists.txt.
 *
 * \see spotty::LoopbackChannel — там же объяснение, зачем этот плагин нужен.
 * \see docs/PLUGIN_API.md
 */
class LoopbackPlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "loopback.json")
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    /// \copydoc spotty::IInterfacePlugin::pluginId
    QString pluginId() const override { return QStringLiteral("loopback"); }

    /// \copydoc spotty::IInterfacePlugin::displayName
    QString displayName() const override { return tr("Loopback"); }

    /// \copydoc spotty::IInterfacePlugin::enumerate
    QList<InterfaceDescriptor> enumerate() const override;

    /// \copydoc spotty::IInterfacePlugin::settingsSchema
    SettingsSchema settingsSchema() const override;

    /// \copydoc spotty::IInterfacePlugin::createChannel
    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override;
};

} // namespace spotty
