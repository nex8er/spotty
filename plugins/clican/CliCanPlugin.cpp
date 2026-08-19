/**
 * \file CliCanPlugin.cpp
 * \brief Реализация spotty::CliCanPlugin.
 */
#include "CliCanPlugin.h"

#include "CliCanChannel.h"
#include "CliCanSettings.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

#include <utility>

namespace spotty {

namespace {

/// \brief Приставка идентификаторов этого плагина.
constexpr auto kIdPrefix = "clican:";

/// \brief Приставка системного имени; из него createChannel() достаёт номер канала PCAN.
constexpr auto kSystemNamePrefix = "PCAN_USBBUS";

/// \brief Как часто проверять, не пора ли отпустить шину поиска.
constexpr int kScanTimerIntervalMs = 1000;

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
    // К этому моменту m_scanOpening уже false почти всегда — finishScanOpen() успевает
    // отработать за секунду между тиками таймера, пока диалог ещё жив. Джойн остаётся
    // подстраховкой: выгрузка модуля плагина с ещё бегущим потоком внутри него —
    // use-after-free на уровне кода, а не данных.
    if (m_scanOpenThread.joinable())
        m_scanOpenThread.join();
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

QList<SettingsOption> CliCanPlugin::liveOptions(const InterfaceDescriptor &descriptor,
                                                const QString &key, const QVariantMap &settings)
{
    if (key != QLatin1String(clican::kNodeKey))
        return {};

    const PcanLibrary::Handle handle = handleFromSystemName(descriptor.systemName);
    if (handle == 0)
        return {};

    const int bitrate = settings.value(QLatin1String(clican::kBitrateKey),
                                       clican::kDefaultBitrate)
                            .toInt();

    // Смена устройства или скорости в открытом диалоге — обычное дело: пользователь
    // перебирает скорости, пока узлы не отзовутся. Старая шина при этом отпускается, и
    // список начинается заново, иначе узлы, найденные на 250k, остались бы висеть в
    // списке для 500k.
    if (m_scanBus && (m_scanHandle != handle || m_scanBitrate != bitrate)) {
        m_scanBus->setDiscoveryEnabled(false);
        m_scanBus.reset();
        m_scanHandle = 0;
        m_scanBitrate = 0;
    }

    if (!m_scanBus && !m_scanOpening)
        beginScanOpen(handle, bitrate);

    m_scanRequested.start();
    if (!m_scanTimer) {
        m_scanTimer = new QTimer(this);
        m_scanTimer->setInterval(kScanTimerIntervalMs);
        connect(m_scanTimer, &QTimer::timeout, this, &CliCanPlugin::expireScan);
    }
    if (!m_scanTimer->isActive())
        m_scanTimer->start();

    QList<SettingsOption> options;
    // Пусто, пока шина ещё открывается в фоновом потоке (см. beginScanOpen()) — тот же
    // случай, что и «драйвер не ответил», и обрабатывается тем же путём: пустой список,
    // следующий тик спросит снова.
    if (m_scanBus) {
        for (int node : m_scanBus->nodes()) {
            // Подпись — голый номер, а не «Node 5»: то же поле разрешено заполнять руками,
            // и набранное «5» обязано совпасть с найденным пунктом, иначе выбор
            // пользователя молча превратился бы в другое значение.
            options.append(SettingsOption{QString::number(node), node});
        }
    }
    return options;
}

void CliCanPlugin::beginScanOpen(PcanLibrary::Handle handle, int bitrate)
{
    // Предыдущий поток к этому моменту гарантированно закончил работу: сюда попадают
    // только когда !m_scanOpening, а флаг снимает finishScanOpen(), исполняющийся уже
    // после того, как фоновый поток отправил результат и вышел из своей функции. Джойн
    // здесь поэтому не ждёт ничего заметного — только освобождает объект std::thread.
    if (m_scanOpenThread.joinable())
        m_scanOpenThread.join();

    m_scanOpening = true;
    m_scanOpenThread = std::thread([this, handle, bitrate] {
        // CAN_Initialize() внутри acquire() — блокирующий вызов драйвера, ради которого
        // и заведён этот поток: liveOptions() зовётся из потока UI и блокировать его не
        // вправе (см. \note в docs/PLUGIN_API.md).
        QString error;
        std::shared_ptr<CanBus> bus = m_pool.acquire(handle, bitrate, &error);
        QMetaObject::invokeMethod(
            this,
            [this, handle, bitrate, bus] { finishScanOpen(handle, bitrate, bus); },
            Qt::QueuedConnection);
    });
}

void CliCanPlugin::finishScanOpen(PcanLibrary::Handle handle, int bitrate,
                                  std::shared_ptr<CanBus> bus)
{
    m_scanOpening = false;

    // Пока шина открывалась, пользователь мог отпустить поле (окно закрылось) или
    // переключиться на другое устройство/скорость. В обоих случаях результат уже не
    // нужен: bus, если она успела открыться, закроется сама, когда эта последняя ссылка
    // выйдет из области видимости. Следующий тик — если он ещё будет — заведёт новую,
    // уже верную попытку.
    if (!bus || m_scanBus)
        return;

    m_scanBus = std::move(bus);
    m_scanHandle = handle;
    m_scanBitrate = bitrate;
    m_scanBus->setDiscoveryEnabled(true);
}

void CliCanPlugin::expireScan()
{
    if (!m_scanBus)
        return;

    // Диалог закрыт — вопросов больше нет. Другого признака закрытия у плагина нет и не
    // нужно: пока окно на экране, ядро спрашивает про узлы раз в секунду.
    if (m_scanRequested.isValid() && m_scanRequested.elapsed() < kScanIdleMs)
        return;

    m_scanBus->setDiscoveryEnabled(false);
    m_scanBus.reset();
    m_scanHandle = 0;
    m_scanBitrate = 0;
    if (m_scanTimer)
        m_scanTimer->stop();
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
