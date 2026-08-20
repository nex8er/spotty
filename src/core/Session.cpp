/**
 * \file Session.cpp
 * \brief Реализация spotty::Session.
 */
#include "Session.h"

#include "ChannelWorker.h"
#include "InterfaceRegistry.h"
#include "PluginManager.h"

#include <spotty/api/IDataFilter.h>
#include <spotty/api/IInterfaceChannel.h>
#include <spotty/api/IInterfacePlugin.h>

#include <QLoggingCategory>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace spotty {

/// \brief Категория журналирования: `spotty.session`.
Q_LOGGING_CATEGORY(lcSession, "spotty.session")

namespace {

/// \brief Период пересчёта скорости приёма, мс.
constexpr int kRateIntervalMs = 500;

/// \brief Сколько ждать завершения потока ввода-вывода при закрытии, мс.
constexpr int kThreadStopTimeoutMs = 3000;

/// \brief Период разбора накопленных порций, мс; см. Session::processPendingIncoming().
/// Тот же кадр, что и у перерисовки терминала — совпадение не случайно: раньше плотный
/// поток забивал очередь событий UI-потока мелкими порциями быстрее этого кадра.
constexpr int kIncomingBatchIntervalMs = 16;

} // namespace

Session::Session(PluginManager *plugins, InterfaceRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_plugins(plugins)
    , m_registry(registry)
{
    Q_ASSERT(m_plugins);
    Q_ASSERT(m_registry);

    connect(m_registry, &InterfaceRegistry::interfaceAppeared,
            this, &Session::onInterfaceAppeared);
    connect(m_registry, &InterfaceRegistry::interfaceDisappeared,
            this, &Session::onInterfaceDisappeared);

    // Пока канал открыт, статистику уже публикует m_rateTimer раз в полсекунды. Прямая
    // ретрансляция каждого изменения буфера заставляла строку состояния пересчитывать
    // раскладку на каждый пакет; на Windows при плотном потоке это отнимало время у
    // терминала и усиливало дрожание viewport. В неактивном сеансе единственное изменение
    // (например, загрузка истории) по-прежнему сообщается сразу.
    //
    // Подписка только на собственный буфер: общий принадлежит окну, иначе две сессии
    // докладывали бы об одном и том же дважды.
    connect(&m_ownBuffer, &TerminalBuffer::statisticsChanged,
            this, [this] {
                if (!m_rateTimer->isActive())
                    Q_EMIT statisticsChanged();
            });

    m_packetTimer = new QTimer(this);
    m_packetTimer->setSingleShot(true);
    // Межбайтовая пауза измеряется единицами миллисекунд, поэтому нужен точный таймер:
    // огрубление по умолчанию сделало бы разбиение по таймауту бессмысленным.
    m_packetTimer->setTimerType(Qt::PreciseTimer);
    connect(m_packetTimer, &QTimer::timeout, this, &Session::handlePacketTimeout);

    m_rateTimer = new QTimer(this);
    m_rateTimer->setInterval(kRateIntervalMs);
    connect(m_rateTimer, &QTimer::timeout, this, [this] {
        const qint64 total = m_buffer->bytesReceived();
        m_receiveRateBps = double(total - m_bytesAtLastTick) * 1000.0 / kRateIntervalMs;
        m_bytesAtLastTick = total;
        Q_EMIT statisticsChanged();
    });

    m_incomingBatchTimer = new QTimer(this);
    m_incomingBatchTimer->setSingleShot(true);
    m_incomingBatchTimer->setInterval(kIncomingBatchIntervalMs);
    connect(m_incomingBatchTimer, &QTimer::timeout, this, &Session::processPendingIncoming);
}

Session::~Session()
{
    destroyWorker();
}

void Session::setInterfaceId(const QString &id)
{
    if (m_interfaceId == id)
        return;

    close();
    m_interfaceId = id;
    setState(ChannelState::Closed);
}

bool Session::isActive() const
{
    return m_state == ChannelState::Open || m_state == ChannelState::Opening;
}

Session::Statistics Session::statistics() const
{
    Statistics stats;
    stats.bytesReceived = m_buffer->bytesReceived();
    stats.bytesSent = m_buffer->bytesSent();
    stats.receiveRateBps = m_receiveRateBps;
    stats.errorCount = m_errorCount;
    return stats;
}

void Session::setPacketizerMode(Packetizer::Mode mode)
{
    m_packetizer.setMode(mode);
}

void Session::setPacketizerTimeout(int milliseconds)
{
    m_packetizer.setTimeoutMs(milliseconds);
}

void Session::setPacketizerDelimiter(const QByteArray &delimiter)
{
    m_packetizer.setDelimiter(delimiter);
}

void Session::setPacketizerFixedLength(int length)
{
    m_packetizer.setFixedLength(length);
}

void Session::setEchoEnabled(bool enabled)
{
    m_echoEnabled = enabled;
}

bool Session::createWorker()
{
    const InterfaceEntry *entry = m_registry->entry(m_interfaceId);
    if (!entry) {
        Q_EMIT errorOccurred(tr("Interface \"%1\" is not known.").arg(m_interfaceId));
        return false;
    }

    IInterfacePlugin *plugin = m_plugins->plugin(entry->descriptor.pluginId);
    if (!plugin) {
        Q_EMIT errorOccurred(
            tr("Plugin \"%1\" is not loaded.").arg(entry->descriptor.pluginId));
        return false;
    }

    IInterfaceChannel *channel = plugin->createChannel(entry->descriptor);
    if (!channel) {
        Q_EMIT errorOccurred(tr("Plugin \"%1\" could not create a channel.")
                                 .arg(entry->descriptor.pluginId));
        return false;
    }

    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("spotty-io"));

    m_worker = new ChannelWorker(channel);
    // Работник вместе с каналом переезжает целиком: с этого момента любой их метод
    // выполняется только в потоке ввода-вывода.
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &ChannelWorker::dataReceived, this, &Session::handleIncoming);

    connect(m_worker, &ChannelWorker::stateChanged, this,
            [this](ChannelState state, const QString &detail) { setState(state, detail); });

    connect(m_worker, &ChannelWorker::openFailed, this, [this](const QString &message) {
        ++m_errorCount;
        m_buffer->appendSystemMessage(message, m_source);
        setState(ChannelState::Error, message);
        Q_EMIT errorOccurred(message);
    });

    connect(m_worker, &ChannelWorker::errorOccurred, this, [this](const QString &message) {
        ++m_errorCount;
        m_buffer->appendSystemMessage(message, m_source);
        Q_EMIT statisticsChanged();
        Q_EMIT errorOccurred(message);
    });

    connect(m_worker, &ChannelWorker::controlLinesChanged, this,
            [this](const QVariantMap &lines) {
                m_controlLines = lines;
                Q_EMIT controlLinesChanged(lines);
            });

    connect(m_worker, &ChannelWorker::bytesWritten, this, [this](const QByteArray &data) {
        if (m_echoEnabled)
            m_buffer->append(data, DataDirection::Tx, 0, /*terminatesLine=*/true, m_source);
        Q_EMIT dataLogged(data, DataDirection::Tx);
    });

    m_thread->start();
    return true;
}

void Session::destroyWorker()
{
    if (!m_thread)
        return;

    // Блокирующий вызов только в живой поток: в остановленный он повис бы навсегда,
    // потому что ответить оттуда некому.
    if (m_worker && m_thread->isRunning())
        QMetaObject::invokeMethod(m_worker, &ChannelWorker::close, Qt::BlockingQueuedConnection);

    m_thread->quit();
    if (!m_thread->wait(kThreadStopTimeoutMs)) {
        // Дойти сюда можно только если плагин заблокировался в своём потоке. Убивать
        // поток нельзя — останутся захваченные дескрипторы, — но и молчать нельзя.
        qCWarning(lcSession) << "I/O thread did not stop within"
                             << kThreadStopTimeoutMs << "ms";
    }

    delete m_thread;
    m_thread = nullptr;
    m_worker = nullptr;

    m_packetTimer->stop();
    m_rateTimer->stop();
    m_receiveRateBps = 0.0;
}

void Session::open()
{
    if (m_interfaceId.isEmpty()) {
        Q_EMIT errorOccurred(tr("No interface selected."));
        return;
    }
    if (isActive())
        return;

    const InterfaceEntry *entry = m_registry->entry(m_interfaceId);
    if (!entry || !entry->present) {
        setState(ChannelState::Unavailable, tr("Device is not present"));
        // Устройства нет прямо сейчас, но пользователь захотел его открыть: запоминаем
        // намерение, чтобы открыть само, когда устройство появится.
        m_reopenWhenAvailable = true;
        return;
    }

    const QVariantMap settings = m_registry->settingsFor(m_interfaceId);

    // Обязательные поля проверяются до создания канала, а не после неудачной попытки: без
    // этого, скажем, J-Link RTT без имени целевого чипа честно пытался бы подключиться и
    // получал бы малопонятную ошибку от стороннего SDK вместо простого «заполните поле».
    if (IInterfacePlugin *plugin = m_plugins->plugin(entry->descriptor.pluginId)) {
        const QStringList missing = plugin->settingsSchema().missingRequiredFields(settings);
        if (!missing.isEmpty()) {
            Q_EMIT requiredSettingsMissing(m_interfaceId, missing);
            return;
        }
    }

    destroyWorker();
    if (!createWorker())
        return;

    setState(ChannelState::Opening);
    m_packetizer.reset();
    m_bytesAtLastTick = m_buffer->bytesReceived();
    m_rateTimer->start();

    QMetaObject::invokeMethod(m_worker, "open", Qt::QueuedConnection,
                              Q_ARG(QVariantMap, settings));

    m_buffer->appendSystemMessage(tr("--- %1 opened ---").arg(entry->displayName()),
                                  m_source);
    m_reopenWhenAvailable = true;
}

void Session::close()
{
    // Явное закрытие снимает намерение переоткрыть: иначе программа спорила бы с
    // пользователем, снова открывая порт, который он только что закрыл.
    m_reopenWhenAvailable = false;

    if (!m_thread) {
        setState(ChannelState::Closed);
        return;
    }

    // Сперва накопленные, ещё не разобранные порции (иначе они уедут в никуда вместе с
    // остановленным потоком ввода-вывода), затем остаток пакетизатора — иначе последнее
    // сообщение пропало бы.
    flushPendingIncoming();
    handlePacketTimeout();

    destroyWorker();
    m_buffer->appendSystemMessage(tr("--- closed ---"), m_source);
    setState(ChannelState::Closed);
}

void Session::send(const QByteArray &data)
{
    if (!m_worker || m_state != ChannelState::Open) {
        Q_EMIT errorOccurred(tr("The interface is not open."));
        return;
    }

    // Передающая цепочка идёт в обратную сторону, как в стеке протоколов: что приёмное
    // звено развернуло последним, передающее сворачивает первым.
    QByteArray effective = data;
    for (auto it = m_filters.crbegin(); it != m_filters.crend(); ++it) {
        effective = it->filter->filterOutgoing(effective);
        // Пустой результат — решение звена не отправлять, а не сбой: сообщать об ошибке
        // здесь значило бы жаловаться на то, о чём попросили.
        if (effective.isEmpty())
            return;
    }

    QMetaObject::invokeMethod(m_worker, "write", Qt::QueuedConnection,
                              Q_ARG(QByteArray, effective));
}

void Session::setSharedBuffer(TerminalBuffer *buffer, quint8 source)
{
    // Иначе то, что накопилось для старого #m_buffer, разберётся уже после подмены
    // указателя и попадёт не туда.
    flushPendingIncoming();
    m_buffer = buffer ? buffer : &m_ownBuffer;
    m_source = buffer ? source : 0;
}

void Session::addDataFilter(IDataFilter *filter, int order, const QString &name)
{
    if (!filter)
        return;
    for (const FilterSlot &slot : std::as_const(m_filters)) {
        if (slot.filter == filter)
            return;
    }

    m_filters.append({order, name, filter});
    std::stable_sort(m_filters.begin(), m_filters.end(),
                     [](const FilterSlot &lhs, const FilterSlot &rhs) {
                         if (lhs.order != rhs.order)
                             return lhs.order < rhs.order;
                         return lhs.name < rhs.name;
                     });
}

void Session::removeDataFilter(IDataFilter *filter)
{
    m_filters.removeIf([filter](const FilterSlot &slot) { return slot.filter == filter; });
}

void Session::reloadSettings()
{
    if (!m_worker || m_state != ChannelState::Open)
        return;

    const QVariantMap settings = m_registry->settingsFor(m_interfaceId);
    QMetaObject::invokeMethod(m_worker, "applySettings", Qt::QueuedConnection,
                              Q_ARG(QVariantMap, settings));
}

void Session::setControlLine(const QString &name, bool asserted)
{
    if (!m_worker)
        return;
    QMetaObject::invokeMethod(m_worker, "setControlLine", Qt::QueuedConnection,
                              Q_ARG(QString, name), Q_ARG(bool, asserted));
}

void Session::sendBreak(int milliseconds)
{
    if (!m_worker)
        return;
    QMetaObject::invokeMethod(m_worker, "sendBreak", Qt::QueuedConnection,
                              Q_ARG(int, milliseconds));
}

void Session::handleIncoming(const QByteArray &data, qint64 monotonicNs)
{
    m_pendingIncoming.append({data, monotonicNs});
    if (!m_incomingBatchTimer->isActive())
        m_incomingBatchTimer->start();
}

void Session::processPendingIncoming()
{
    if (m_pendingIncoming.isEmpty())
        return;

    // Список освобождается сразу: если разбор одной из порций (через сигналы наружу)
    // каким-то образом приведёт к новому handleIncoming(), она обязана попасть в
    // следующий проход, а не потеряться и не разобраться дважды в этом же.
    const QList<IncomingChunk> pending = std::exchange(m_pendingIncoming, {});

    for (const IncomingChunk &chunk : pending) {
        // Наблюдателям отдаём поток до пакетизации и до цепочки: журнал должен получить
        // ровно то, что пришло по проводу, а не то, что из этого сделали плагины. Каждая
        // порция — отдельным сигналом, как и раньше: LogWriter и подписчики dataReceived()
        // не должны увидеть чужую склейку вместо границ настоящих чтений.
        Q_EMIT dataLogged(chunk.data, DataDirection::Rx);
        Q_EMIT dataReceived(chunk.data, chunk.monotonicNs);

        QByteArray effective = chunk.data;
        for (const FilterSlot &slot : std::as_const(m_filters)) {
            effective = slot.filter->filterIncoming(effective, chunk.monotonicNs);
            if (effective.isEmpty())
                break;
        }

        // Порция проглочена целиком — пакетизатору её отдавать нечего.
        if (effective.isEmpty())
            continue;

        const QList<Packetizer::Packet> packets = m_packetizer.feed(effective, chunk.monotonicNs);
        for (const Packetizer::Packet &packet : packets)
            m_buffer->append(packet.data, DataDirection::Rx, chunk.monotonicNs,
                             packet.terminatesLine, m_source);
    }

    // Ждать паузу имеет смысл только когда что-то накоплено и правило этого требует.
    // Перезапуск одним разом на весь проход, а не после каждой порции внутри цикла, ведёт
    // к тому же результату — таймер всё равно считает от последнего запуска, — но дешевле.
    if (m_packetizer.mode() == Packetizer::Mode::InterByteTimeout
        && m_packetizer.hasPending()) {
        m_packetTimer->start(m_packetizer.timeoutMs());
    }
}

void Session::flushPendingIncoming()
{
    m_incomingBatchTimer->stop();
    processPendingIncoming();
}

void Session::handlePacketTimeout()
{
    const Packetizer::Packet packet = m_packetizer.flush();
    if (!packet.data.isEmpty())
        m_buffer->append(packet.data, DataDirection::Rx, 0, /*terminatesLine=*/true,
                         m_source);
}

void Session::setState(ChannelState state, const QString &detail)
{
    if (m_state == state && m_stateDetail == detail)
        return;

    m_state = state;
    m_stateDetail = detail;
    Q_EMIT stateChanged(m_state, m_stateDetail);
}

void Session::onInterfaceAppeared(const QString &id)
{
    if (id != m_interfaceId || !m_reopenWhenAvailable || isActive())
        return;

    qCInfo(lcSession) << "reopening" << id << "after it came back";
    m_buffer->appendSystemMessage(tr("--- device is back, reopening ---"), m_source);
    open();
}

void Session::onInterfaceDisappeared(const QString &id)
{
    if (id != m_interfaceId || !m_thread)
        return;

    qCInfo(lcSession) << "lost" << id;

    // Намерение переоткрыть сохраняется: устройство пропало не по воле пользователя.
    const bool shouldReopen = m_reopenWhenAvailable;
    destroyWorker();
    m_reopenWhenAvailable = shouldReopen;

    m_buffer->appendSystemMessage(tr("--- device disconnected ---"), m_source);
    setState(ChannelState::Unavailable, tr("Device disconnected"));
}

} // namespace spotty
