/**
 * \file JlinkRttPlugin.h
 * \brief Плагин RTT поверх SEGGER J-Link.
 */
#pragma once

#include <spotty/api/IInterfacePlugin.h>

#include <QObject>

namespace spotty {

/**
 * \class JlinkRttPlugin
 * \brief Устройства — не физические порты, а каналы (буферы) RTT.
 *
 * \par Что такое «устройство» здесь
 *
 * У обычного зонда одна RTT-сессия может нести несколько независимых буферов
 * (канал 0 — обычно текстовый «Terminal», остальные — под нужды прошивки). Число буферов,
 * которые прошивка реально завела, до подключения неизвестно и недёшево выяснять на
 * каждом enumerate() (он вызывается раз в секунду и обязан быть быстрым — см.
 * IInterfacePlugin::enumerate()). Поэтому вместо однократного опроса таргета плагин
 * перечисляет фиксированный запас номеров буферов на каждый найденный зонд, а
 * неиспользуемые пользователь скрывает — тем же способом, что и лишний UART-порт.
 *
 * \see spotty::JlinkRttChannel
 * \see spotty::JlinkArmLibrary
 */
class JlinkRttPlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "jlinkrtt.json")
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    /// \copydoc spotty::IInterfacePlugin::pluginId
    QString pluginId() const override { return QStringLiteral("jlinkrtt"); }

    /// \copydoc spotty::IInterfacePlugin::displayName
    QString displayName() const override { return tr("J-Link RTT"); }

    /// \copydoc spotty::IInterfacePlugin::enumerate
    QList<InterfaceDescriptor> enumerate() const override;

    /// \copydoc spotty::IInterfacePlugin::settingsSchema
    SettingsSchema settingsSchema() const override;

    /// \brief Выжимка вида `"SWD 4000 kHz"`.
    QString settingsSummary(const QVariantMap &settings) const override;

    /// \copydoc spotty::IInterfacePlugin::createChannel
    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override;
};

} // namespace spotty
