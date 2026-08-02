/**
 * \file UartPlugin.h
 * \brief Плагин последовательных портов.
 */
#pragma once

#include <spotty/api/IInterfacePlugin.h>

#include <QObject>

namespace spotty {

/**
 * \class UartPlugin
 * \brief Поддержка последовательных портов (COM, tty) поверх Qt SerialPort.
 *
 * \see spotty::UartChannel
 * \see docs/PLUGIN_API.md
 */
class UartPlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "uart.json")
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    /// \copydoc spotty::IInterfacePlugin::pluginId
    QString pluginId() const override { return QStringLiteral("uart"); }

    /// \copydoc spotty::IInterfacePlugin::displayName
    QString displayName() const override { return tr("Serial / UART"); }

    /// \copydoc spotty::IInterfacePlugin::enumerate
    QList<InterfaceDescriptor> enumerate() const override;

    /// \copydoc spotty::IInterfacePlugin::settingsSchema
    SettingsSchema settingsSchema() const override;

    /// \brief Выжимка вида `"115200 8-N-1"`.
    QString settingsSummary(const QVariantMap &settings) const override;

    /// \copydoc spotty::IInterfacePlugin::createChannel
    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override;
};

} // namespace spotty
