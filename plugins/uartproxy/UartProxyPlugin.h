/**
 * \file UartProxyPlugin.h
 * \brief Плагин перехвата последовательного порта.
 */
#pragma once

#include <spotty/api/IInterfacePlugin.h>

#include <QObject>

namespace spotty {

/**
 * \class UartProxyPlugin
 * \brief Встаёт между устройством и чужой программой и показывает весь их обмен.
 *
 * \par Зачем он так устроен
 *
 * Порт, открытый другой программой, второй программой не открыть, а пассивно подслушать
 * обычными средствами нельзя ни на одной из систем. Остаётся стать посредником: Spotty
 * открывает реальный порт сам и подставляет программе виртуальный (spotty::VirtualPort).
 * Цена — программу придётся один раз перенастроить на виртуальный порт и запускать после
 * Spotty; выгода — работает одинаково без драйверов и без железа.
 *
 * \par Один интерфейс, а не по штуке на порт
 *
 * Реальный порт выбирается в настройках, а не перечисляется отдельными устройствами: иначе
 * в списке каждый порт встал бы дважды, обычным и перехваченным, и различать их пришлось
 * бы по приписке.
 *
 * \see spotty::UartProxyChannel
 */
class UartProxyPlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "uartproxy.json")
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    QString pluginId() const override { return QStringLiteral("uartproxy"); }
    QString displayName() const override { return tr("Serial proxy"); }
    QList<InterfaceDescriptor> enumerate() const override;
    SettingsSchema settingsSchema() const override;

    /// \brief Выжимка вида `"115200 8-N-1 → /tmp/spotty-uart"`.
    QString settingsSummary(const QVariantMap &settings) const override;

    /// \brief Список портов для полей выбора порта; пока диалог открыт, обновляется.
    QList<SettingsOption> liveOptions(const InterfaceDescriptor &descriptor, const QString &key,
                                      const QVariantMap &settings) override;

    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override;
};

} // namespace spotty
