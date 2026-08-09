/**
 * \file SignalGenPlugin.h
 * \brief Плагин виртуального источника математических сигналов.
 */
#pragma once

#include <spotty/api/IInterfacePlugin.h>

#include <QObject>

namespace spotty {

/**
 * \class SignalGenPlugin
 * \brief Единственное виртуальное устройство, все режимы которого — в настройках.
 *
 * В отличие от spotty::LoopbackPlugin, где по устройству различаются echo и chatter, здесь
 * разнообразие целиком в схеме настроек одного устройства: перебирать форму сигнала удобнее
 * из уже открытого диалога, не переоткрывая канал через другой пункт списка интерфейсов.
 *
 * \see spotty::SignalGenChannel — там же описан формат выдаваемых строк.
 */
class SignalGenPlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "signalgen.json")
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    /// \copydoc spotty::IInterfacePlugin::pluginId
    QString pluginId() const override { return QStringLiteral("signalgen"); }

    /// \copydoc spotty::IInterfacePlugin::displayName
    QString displayName() const override { return tr("Signal generator"); }

    /// \copydoc spotty::IInterfacePlugin::enumerate
    QList<InterfaceDescriptor> enumerate() const override;

    /// \copydoc spotty::IInterfacePlugin::settingsSchema
    SettingsSchema settingsSchema() const override;

    /// \copydoc spotty::IInterfacePlugin::settingsSummary
    QString settingsSummary(const QVariantMap &settings) const override;

    /// \copydoc spotty::IInterfacePlugin::createChannel
    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override;
};

} // namespace spotty
