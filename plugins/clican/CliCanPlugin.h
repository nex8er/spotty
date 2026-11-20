/**
 * \file CliCanPlugin.h
 * \brief Плагин командной строки платы через туннель CAN.
 */
#pragma once

#include "CanBus.h"

#include <spotty/api/IInterfacePlugin.h>
#include <spotty/api/INodeDiscovery.h>

#include <QElapsedTimer>
#include <QObject>

#include <memory>
#include <thread>

class QTimer;

namespace spotty {

/**
 * \class CliCanPlugin
 * \brief Доступ к CLI плат, подключённых к шине CAN, через адаптер PCAN.
 *
 * Устройство здесь — адаптер, а не плата: скорость шины нужно знать раньше, чем на ней
 * удастся кого-то найти. Плата выбирается настройкой «Node», список которой заполняется
 * опросом (SettingsField::live) — диалог открывается сразу, а узлы появляются в нём по
 * мере ответов.
 *
 * \see spotty::CliCanChannel
 * \see docs/CLICAN.md
 */
class CliCanPlugin : public QObject, public IInterfacePlugin, public INodeDiscovery
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "clican.json")
    Q_INTERFACES(spotty::IInterfacePlugin spotty::INodeDiscovery)

public:
    CliCanPlugin();
    ~CliCanPlugin() override;

    /// \copydoc spotty::IInterfacePlugin::pluginId
    QString pluginId() const override { return QStringLiteral("clican"); }

    /// \copydoc spotty::IInterfacePlugin::displayName
    QString displayName() const override { return tr("CLI over CAN (PCAN)"); }

    /// \copydoc spotty::IInterfacePlugin::enumerate
    QList<InterfaceDescriptor> enumerate() const override;

    /// \copydoc spotty::IInterfacePlugin::settingsSchema
    SettingsSchema settingsSchema() const override;

    /// \brief Выжимка вида `"node 5, 500 kbit/s"`.
    QString settingsSummary(const QVariantMap &settings) const override;

    /// \copydoc spotty::IInterfacePlugin::liveOptions
    QList<SettingsOption> liveOptions(const InterfaceDescriptor &descriptor, const QString &key,
                                      const QVariantMap &settings) override;

    /// \copydoc spotty::IInterfacePlugin::createChannel
    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override;

    QString nodeSettingKey() const override;
    QList<NetworkNode> networkNodes(const InterfaceDescriptor &descriptor,
                                   const QVariantMap &settings, bool discover) override;
    void clearNetworkNodes(const InterfaceDescriptor &descriptor) override;

private:
    /// \brief Поиск адаптеров независим; результат потока читается после join().
    struct Scan
    {
        std::shared_ptr<CanBus> bus;
        std::shared_ptr<CanBus> openedBus;
        std::thread thread;
        std::atomic<bool> opening{false};
        int bitrate = 0;
        bool panelActive = false;
        QElapsedTimer settingsRequested;
        QString error;
    };

    Scan *requestScan(const InterfaceDescriptor &descriptor, const QVariantMap &settings);
    void expireScan();

    /// \brief Пул разрушается после ссылок на шины и завершения потоков открытия.
    CanBusPool m_pool;
    std::map<PcanLibrary::Handle, std::unique_ptr<Scan>> m_scans;
    QTimer *m_scanTimer = nullptr;
    static constexpr int kScanIdleMs = 3000;
};

} // namespace spotty
