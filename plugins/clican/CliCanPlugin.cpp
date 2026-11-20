/**
 * \file CliCanPlugin.cpp
 * \brief Реализация spotty::CliCanPlugin.
 */
#include "CliCanPlugin.h"

#include "CliCanChannel.h"
#include "CliCanSettings.h"

#include <QCoreApplication>
#include <QTimer>

#include <utility>

namespace spotty {

namespace {

/// \brief Приставка идентификаторов этого плагина.
constexpr auto kIdPrefix = "clican:";

/// \brief Приставка системного имени; из него createChannel() достаёт номер канала PCAN.
constexpr auto kSystemNamePrefix = "PCAN_USBBUS";

/// \brief Как часто проверять, не пора ли отпустить шину поиска.
constexpr int kScanTimerIntervalMs = 50;

/**
 * \brief Устойчивый идентификатор адаптера.
 *
 * Номер устройства (`PCAN_DEVICE_ID`) назначает пользователь утилитой PEAK, и он живёт в
 * самом адаптере — это единственное, что переживает перестановку в другой порт USB. Пока
 * он не задан (а по умолчанию он не задан), различить два одинаковых адаптера нечем, и
 * приходится опускаться до номера канала: он зависит от порядка подключения, но другого
 * признака у железа просто нет. Ровно так же поступает UART, когда переходник не сообщает
 * серийного номера.
 */
QString buildStableId(const PcanLibrary::ChannelInfo &info)
{
    if (info.hasDeviceId())
        return QStringLiteral("%1dev%2").arg(QLatin1String(kIdPrefix)).arg(info.deviceId);
    return QStringLiteral("%1usbbus%2").arg(QLatin1String(kIdPrefix)).arg(info.index);
}

/**
 * \brief Ручка канала PCAN, зашитая в системное имя устройства.
 * \return 0, если имя чужое или номер канала вне поддерживаемого диапазона.
 *
 * Системное имя — единственное, что реестр сохраняет из дескриптора помимо
 * идентификатора и псевдонима: поля #InterfaceDescriptor::extra не переживают
 * перезапуск, и класть номер канала туда было бы ошибкой, заметной только после
 * перезапуска с отключённым адаптером.
 */
PcanLibrary::Handle handleFromSystemName(const QString &systemName)
{
    if (!systemName.startsWith(QLatin1String(kSystemNamePrefix)))
        return 0;

    bool ok = false;
    const int index = QStringView(systemName).sliced(qstrlen(kSystemNamePrefix)).toInt(&ok);
    return ok ? PcanLibrary::usbBusHandle(index) : PcanLibrary::Handle(0);
}

/// \brief Читаемая подпись скорости: «500 kbit/s», «1 Mbit/s».
QString bitrateLabel(int bitsPerSecond)
{
    if (bitsPerSecond >= 1'000'000 && bitsPerSecond % 1'000'000 == 0)
        return QCoreApplication::translate("spotty::CliCanPlugin", "%1 Mbit/s")
            .arg(bitsPerSecond / 1'000'000);
    return QCoreApplication::translate("spotty::CliCanPlugin", "%1 kbit/s")
        .arg(bitsPerSecond / 1000.0, 0, 'g', 5);
}

} // namespace

CliCanPlugin::CliCanPlugin() = default;

CliCanPlugin::~CliCanPlugin()
{
    for (auto &[handle, scan] : m_scans) {
        if (scan->thread.joinable())
            scan->thread.join();
        if (scan->bus)
            scan->bus->setDiscoveryEnabled(false);
    }
}

QList<InterfaceDescriptor> CliCanPlugin::enumerate() const
{
    QList<InterfaceDescriptor> result;

    // Кэша здесь нет намеренно: опрос восьми ручек PCAN стоит десятки микросекунд
    // (замерено на macOS/arm64), то есть дешевле самого кэша. Требование «enumerate()
    // обязан быть дешёвым» выполняется без ухищрений — в отличие от, скажем, обхода сети.
    const QList<PcanLibrary::ChannelInfo> channels = PcanLibrary::instance().availableChannels();
    result.reserve(channels.size());

    for (const PcanLibrary::ChannelInfo &info : channels) {
        InterfaceDescriptor descriptor;
        descriptor.id = buildStableId(info);
        descriptor.systemName = QLatin1String(kSystemNamePrefix) + QString::number(info.index);
        descriptor.description = info.name;

        if (info.hasDeviceId())
            descriptor.extra.insert(QStringLiteral("deviceId"), info.deviceId);
        if (info.occupied) {
            descriptor.extra.insert(QStringLiteral("state"),
                                    tr("in use by another application"));
        }
        descriptor.extra.insert(QStringLiteral("driver"),
                                PcanLibrary::instance().apiVersion());

        result.append(descriptor);
    }

    return result;
}

SettingsSchema CliCanPlugin::settingsSchema() const
{
    SettingsSchema schema;

    const QString busGroup = tr("CAN bus");
    const QString tunnelGroup = tr("Tunnel");

    QList<SettingsOption> bitrates;
    for (int bitrate : PcanLibrary::supportedBitrates())
        bitrates.append(SettingsOption{bitrateLabel(bitrate), bitrate});

    schema.add(SettingsField{
        .key = QLatin1String(clican::kBitrateKey),
        .label = tr("Bit rate"),
        .group = busGroup,
        .type = SettingsField::Choice,
        .defaultValue = clican::kDefaultBitrate,
        .options = bitrates,
        // Список закрыт: классический PCAN принимает не скорость, а пару регистров
        // тайминга, и произвольное число драйверу передать нечем.
        .editable = false,
    });

    schema.add(SettingsField{
        .key = QLatin1String(clican::kNodeKey),
        .label = tr("Node"),
        .group = busGroup,
        .type = SettingsField::Choice,
        .defaultValue = 0,
        // Пусто до первого ответа: узлы находятся опросом уже после открытия диалога.
        .options = {},
        .live = true,
        // Узел, который сейчас молчит (плата в перезагрузке), иначе нельзя было бы выбрать
        // вовсе, хотя номер его пользователю известен.
        .editable = true,
        .hint = tr("Boards answer the broadcast query while this window is open. "
                   "A node that is silent right now can be entered by number."),
    });

    schema.add(SettingsField{
        .key = QLatin1String(clican::kKeepAliveKey),
        .label = tr("Keep-alive"),
        .group = tunnelGroup,
        .type = SettingsField::Integer,
        .defaultValue = clican::kDefaultKeepAliveMs,
        .minimum = 100,
        .maximum = clican::kTunnelHoldMs - 100,
        .suffix = tr("ms"),
        .hint = tr("A board leaves tunnelling mode %1 ms after the last packet addressed "
                   "to it and goes back to its own UART.")
                    .arg(clican::kTunnelHoldMs),
    });

    schema.add(SettingsField{
        .key = QLatin1String(clican::kTimeoutKey),
        .label = tr("Response timeout"),
        .group = tunnelGroup,
        .type = SettingsField::Integer,
        .defaultValue = clican::kDefaultTimeoutMs,
        .minimum = 0,
        .maximum = 60'000,
        .suffix = tr("ms"),
        .hint = tr("Warn when the node stays silent for this long. The channel stays "
                   "open - a rebooting board comes back on its own. Zero disables it."),
    });

    schema.add(SettingsField{
        .key = QLatin1String(clican::kScanDurationKey),
        .label = tr("Scan duration"),
        .group = tr("Scan"),
        .type = SettingsField::Integer,
        .defaultValue = clican::kDefaultScanDurationMs,
        .minimum = 1,
        .maximum = 60000,
        .suffix = tr("ms"),
    });
    return schema;
}

QString CliCanPlugin::settingsSummary(const QVariantMap &settings) const
{
    if (settings.isEmpty())
        return {};

    const int node = settings.value(QLatin1String(clican::kNodeKey)).toInt();
    const int bitrate = settings.value(QLatin1String(clican::kBitrateKey)).toInt();

    if (!clican::isValidNode(node))
        return tr("no node, %1").arg(bitrateLabel(bitrate));
    return tr("node %1, %2").arg(node).arg(bitrateLabel(bitrate));
}

QString CliCanPlugin::nodeSettingKey() const
{
    return QLatin1String(clican::kNodeKey);
}

CliCanPlugin::Scan *CliCanPlugin::requestScan(const InterfaceDescriptor &descriptor,
                                            const QVariantMap &settings)
{
    const auto handle = handleFromSystemName(descriptor.systemName);
    if (!handle)
        return nullptr;
    auto &entry = m_scans[handle];
    if (!entry)
        entry = std::make_unique<Scan>();
    Scan *scan = entry.get();
    if (scan->mode == NodeDiscoveryMode::Disconnected && !scan->pendingScanMs
        && scan->manualUntil.hasExpired())
        return nullptr;
    const int bitrate = settings.value(QLatin1String(clican::kBitrateKey),
                                        clican::kDefaultBitrate).toInt();
    if (scan->opening.load())
        return scan;
    if (scan->thread.joinable()) {
        scan->thread.join();
        scan->bus = std::move(scan->openedBus);
    }
    if (scan->bus && scan->bitrate != bitrate) {
        scan->bus->setDiscoveryEnabled(false);
        scan->opening = true;
        Q_ASSERT(!scan->thread.joinable());
        scan->thread = std::thread([scan, bus = std::move(scan->bus)]() mutable {
            bus.reset();
            scan->opening = false;
        });
    }
    if (scan->thread.joinable() || scan->opening.load())
        return scan;
    if (!scan->bus) {
        if (!scan->pendingScanMs) scan->manualOnly = false;
        scan->bitrate = bitrate;
        scan->opening = true;
        Q_ASSERT(!scan->thread.joinable());
        scan->thread = std::thread([this, scan, handle, bitrate] {
            scan->openedBus = m_pool.acquire(handle, bitrate, &scan->error);
            scan->opening = false;
        });
    }
    if (!m_scanTimer) {
        m_scanTimer = new QTimer(this);
        m_scanTimer->setInterval(kScanTimerIntervalMs);
        connect(m_scanTimer, &QTimer::timeout, this, &CliCanPlugin::expireScan);
        m_scanTimer->start();
    }
    return scan;
}

QList<SettingsOption> CliCanPlugin::liveOptions(const InterfaceDescriptor &descriptor,
                                              const QString &key, const QVariantMap &settings)
{
    if (key != nodeSettingKey())
        return {};
    Scan *scan = requestScan(descriptor, settings);
    if (!scan)
        return {};
    scan->settingsRequested.start();
    QList<SettingsOption> options;
    if (scan->bus) {
        if (scan->manualUntil.hasExpired() && !scan->pendingScanMs)
            scan->bus->setDiscoveryEnabled(scan->mode == NodeDiscoveryMode::Active
                                           && !scan->manualOnly);
        for (int node : scan->bus->nodes())
            options.append({QString::number(node), node});
    }
    return options;
}

QList<NetworkNode> CliCanPlugin::networkNodes(const InterfaceDescriptor &descriptor,
                                            const QVariantMap &settings, bool discover)
{
    const auto handle = handleFromSystemName(descriptor.systemName);
    Scan *scan = nullptr;
    const auto existingScan = m_scans.find(handle);
    if (existingScan != m_scans.end()
        && existingScan->second->mode == NodeDiscoveryMode::Disconnected)
        discover = false;
    if (discover) {
        QVariantMap effectiveSettings = settings;
        auto it = m_scans.find(handle);
        if (it != m_scans.end()) {
            const auto &existing = it->second;
            /** \brief Пока открыты настройки, их черновая скорость имеет приоритет.
             * Иначе панель и диалог переоткрывали бы одну шину на разных скоростях.
             */
            if (existing->settingsRequested.isValid()
                && existing->settingsRequested.elapsed() < kScanIdleMs)
                effectiveSettings.insert(QLatin1String(clican::kBitrateKey), existing->bitrate);
        }
        scan = requestScan(descriptor, effectiveSettings);
    } else if (auto it = m_scans.find(handle); it != m_scans.end()) {
        scan = it->second.get();
    }
    if (!scan)
        return {};
    scan->panelActive = discover;
    if (scan->bus) {
        const bool settingsActive = scan->settingsRequested.isValid()
                                    && scan->settingsRequested.elapsed() < kScanIdleMs;
        if (scan->manualUntil.hasExpired() && !scan->pendingScanMs)
            scan->bus->setDiscoveryEnabled(scan->mode == NodeDiscoveryMode::Active
                                          && !scan->manualOnly && (discover || settingsActive));
        return scan->bus->networkNodes();
    }
    return {};
}

void CliCanPlugin::scanOnce(const InterfaceDescriptor &descriptor, const QVariantMap &settings)
{
    const auto handle = handleFromSystemName(descriptor.systemName);
    if (!handle) return;
    auto &entry = m_scans[handle];
    if (!entry) entry = std::make_unique<Scan>();
    entry->pendingScanMs = qBound(1, settings.value(QLatin1String(clican::kScanDurationKey),
                                               clican::kDefaultScanDurationMs).toInt(), 60000);
    entry->manualOnly = true;
    if (entry->bus) entry->bus->setDiscoveryEnabled(false);
    requestScan(descriptor, settings);
    expireScan();
}

void CliCanPlugin::setDiscoveryMode(const InterfaceDescriptor &descriptor, NodeDiscoveryMode mode)
{
    const auto handle = handleFromSystemName(descriptor.systemName);
    if (!handle) return;
    auto &entry = m_scans[handle];
    if (!entry) entry = std::make_unique<Scan>();
    if (entry->mode == mode) return;
    entry->mode = mode;
    if (mode != NodeDiscoveryMode::Active && entry->bus)
        entry->bus->setDiscoveryEnabled(false);
    if (mode == NodeDiscoveryMode::Disconnected) {
        entry->pendingScanMs = 0;
        entry->manualUntil = QDeadlineTimer(0);
        entry->panelActive = false;
        entry->settingsRequested.invalidate();
        expireScan();
    }
}

void CliCanPlugin::clearNetworkNodes(const InterfaceDescriptor &descriptor)
{
    auto it = m_scans.find(handleFromSystemName(descriptor.systemName));
    if (it != m_scans.end() && it->second->bus)
        it->second->bus->clearNetworkNodes();
}

void CliCanPlugin::expireScan()
{
    for (auto &[handle, entry] : m_scans) {
        Scan *scan = entry.get();
        const bool settingsActive = scan->settingsRequested.isValid()
                                    && scan->settingsRequested.elapsed() < kScanIdleMs;
        if (scan->opening.load()) continue;
        if (scan->thread.joinable()) {
            scan->thread.join();
            scan->bus = std::move(scan->openedBus);
        }
        if (scan->pendingScanMs && scan->bus) {
            scan->bus->setDiscoveryEnabled(true, scan->pendingScanMs);
            scan->manualUntil = QDeadlineTimer(scan->pendingScanMs);
        }
        scan->pendingScanMs = 0;
        if (!scan->manualUntil.hasExpired()) continue;
        if (scan->manualOnly && scan->bus) scan->bus->setDiscoveryEnabled(false);
        if (scan->mode != NodeDiscoveryMode::Disconnected
            && (scan->panelActive || settingsActive)) continue;
        if (scan->bus) {
            scan->bus->setDiscoveryEnabled(false);
            scan->opening = true;
            Q_ASSERT(!scan->thread.joinable());
            scan->thread = std::thread([scan, bus = std::move(scan->bus)]() mutable {
                bus.reset();
                scan->opening = false;
            });
        }
    }
}

IInterfaceChannel *CliCanPlugin::createChannel(const InterfaceDescriptor &descriptor)
{
    if (!descriptor.id.startsWith(QLatin1String(kIdPrefix)))
        return nullptr;

    // Открывать канал нужно по системному имени, а не по идентификатору: идентификатор
    // намеренно от номера канала не зависит, когда адаптеру задан номер устройства.
    const PcanLibrary::Handle handle = handleFromSystemName(descriptor.systemName);
    if (handle == 0)
        return nullptr;

    return new CliCanChannel(handle, &m_pool);
}

} // namespace spotty
