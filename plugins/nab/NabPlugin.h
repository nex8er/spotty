/**
 * \file NabPlugin.h
 * \brief Плагин USB-интерфейсов NAB.
 */
#pragma once

#include <spotty/api/IInterfacePlugin.h>

#include <QObject>

namespace spotty {

/**
 * \class NabPlugin
 * \brief Отдельные vendor-specific bulk-интерфейсы USB-устройства NAB.
 *
 * Устройство предоставляет два независимых интерфейса с собственной парой IN/OUT
 * endpoint-ов. Плагин показывает каждый отдельной записью, чтобы их можно было открыть в
 * разных сессиях Spotty одновременно.
 */
class NabPlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "nab.json")
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    /// \copydoc spotty::IInterfacePlugin::pluginId
    QString pluginId() const override { return QStringLiteral("nab"); }

    /// \copydoc spotty::IInterfacePlugin::displayName
    QString displayName() const override { return tr("NAB USB"); }

    /// \copydoc spotty::IInterfacePlugin::enumerate
    QList<InterfaceDescriptor> enumerate() const override;

    /// \copydoc spotty::IInterfacePlugin::settingsSchema
    SettingsSchema settingsSchema() const override { return {}; }

    /// \copydoc spotty::IInterfacePlugin::createChannel
    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override;
};

} // namespace spotty
