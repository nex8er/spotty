/**
 * \file JsonGenPlugin.h
 * \brief Плагин виртуального источника JSON.
 */
#pragma once

#include <spotty/api/IInterfacePlugin.h>

#include <QObject>

namespace spotty {

/**
 * \class JsonGenPlugin
 * \brief Единственное виртуальное устройство, шлющее JSON; все режимы — в настройках.
 *
 * Та же роль, что у spotty::SignalGenPlugin при разработке графика: панель разбора JSON
 * нечем проверять без устройства, а формы, на которых она обязана не сломаться, у реального
 * устройства по заказу не получишь. Здесь они переключаются в диалоге настроек — форма
 * документа, разная частота у разных полей, помехи, вывод с отступами и меняющаяся на ходу
 * структура.
 *
 * \see spotty::JsonGenChannel — там описано, что именно выдаётся.
 */
class JsonGenPlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "jsongen.json")
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    /// \copydoc spotty::IInterfacePlugin::pluginId
    QString pluginId() const override { return QStringLiteral("jsongen"); }

    /// \copydoc spotty::IInterfacePlugin::displayName
    QString displayName() const override { return tr("JSON source"); }

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
