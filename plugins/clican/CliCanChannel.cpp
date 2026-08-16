/**
 * \file CliCanChannel.cpp
 * \brief Реализация spotty::CliCanChannel.
 */
#include "CliCanChannel.h"

#include "CliCanSettings.h"

#include <QTimer>

namespace spotty {

namespace {

/**
 * \brief Как часто канал делает свою работу по обслуживанию туннеля.
 *
 * Не совпадает с интервалом удержания: тик ещё и досылает отложенное паузой шлюза, и
 * замечает молчание узла, а оба срока задаёт пользователь.
 */
constexpr int kServiceIntervalMs = 100;

} // namespace

CliCanChannel::CliCanChannel(PcanLibrary::Handle handle, CanBusPool *pool, QObject *parent)
    : IInterfaceChannel(parent)
    , m_handle(handle)
    , m_pool(pool)
{
}

CliCanChannel::~CliCanChannel()
{
    close();
}

bool CliCanChannel::open(const QVariantMap &settings, QString *error)
{
    m_node = settings.value(QLatin1String(clican::kNodeKey)).toInt();
    if (!clican::isValidNode(m_node)) {
        if (error) {
            *error = tr("Select a node between %1 and %2: the CAN tunnel addresses a "
                        "specific board, not the bus as a whole.")
                         .arg(clican::kMinNode)
                         .arg(clican::kMaxNode);
        }
        return false;
    }

    const int bitrate = settings.value(QLatin1String(clican::kBitrateKey)).toInt();
    m_keepAliveMs = settings.value(QLatin1String(clican::kKeepAliveKey)).toInt();
    m_responseTimeoutMs = settings.value(QLatin1String(clican::kTimeoutKey)).toInt();

    m_bus = m_pool->acquire(m_handle, bitrate, error);
    if (!m_bus)
        return false;

    m_clock.start();
    m_pending.clear();
    m_pausedUntilMs = 0;
    m_lastResponseMs = 0;
    m_lastFrameSentMs = 0;
    m_silenceReported = false;

    // Обработчик зовётся из потока приёма шины, а канал живёт в потоке ввода-вывода ядра.
    // Перескок делается здесь и явно: очередной вызов на себя доставляется в собственный
    // поток канала, и дальше весь остальной код класса однопоточен. Захватывать this в
    // лямбду безопасно — подписка снимается в close() до разрушения канала, и
    // removeHandler() не возвращается, пока обработчик выполняется.
    m_handlerToken = m_bus->addHandler(
        [this](quint32 id, const QByteArray &data, qint64 monotonicNs) {
            QMetaObject::invokeMethod(
                this, [this, id, data, monotonicNs] { handleFrame(id, data, monotonicNs); },
                Qt::QueuedConnection);
        },
        [this](const QString &message) {
            QMetaObject::invokeMethod(
                this, [this, message] { Q_EMIT errorOccurred(message); },
                Qt::QueuedConnection);
        });

    // Таймер создаётся здесь, а не в конструкторе: к моменту open() канал уже перенесён в
    // поток ввода-вывода, и таймер должен принадлежать именно ему.
    m_timer = new QTimer(this);
    m_timer->setInterval(kServiceIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &CliCanChannel::tick);
    m_timer->start();

    // Первый пакет — сразу: он и переводит плату в режим туннелирования. Пустой, потому что
    // отправлять пользователю пока нечего, а ждать его ввода значило бы не увидеть ни строки
    // вывода до первого нажатия клавиши.
    QString detail;
    if (!m_bus->send(clican::requestId(m_node), QByteArray(), &detail)) {
        close();
        if (error)
            *error = tr("Could not reach node %1: %2").arg(m_node).arg(detail);
        return false;
    }
    m_lastFrameSentMs = m_clock.elapsed();

    setState(ChannelState::Open, tr("Node %1").arg(m_node));
    return true;
}

void CliCanChannel::close()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }

    if (m_bus) {
        // Сначала отписка, потом освобождение шины: removeHandler() возвращается, только
        // когда обработчик заведомо не выполняется, и лишь после этого можно ронять всё,
        // на что он ссылается.
        m_bus->removeHandler(m_handlerToken);
        m_handlerToken = 0;
        m_bus.reset();
    }

    m_pending.clear();
    setState(ChannelState::Closed);
}

qint64 CliCanChannel::write(const QByteArray &data)
{
    if (m_state != ChannelState::Open || !m_bus)
        return -1;
    if (data.isEmpty())
        return 0;

    m_pending.append(data);
    drainQueue();
    return data.size();
}

void CliCanChannel::drainQueue()
{
    if (!m_bus)
        return;

    const qint64 now = m_clock.elapsed();
    if (now < m_pausedUntilMs)
        return;

    while (!m_pending.isEmpty()) {
        const QByteArray chunk = m_pending.left(clican::kMaxPayload);

        QString detail;
        if (!m_bus->send(clican::requestId(m_node), chunk, &detail)) {
            // Очередь передачи контроллера переполнена или шина занята. Остаток остаётся в
            // m_pending и уйдёт следующим тиком: команда, набранная пользователем, не
            // должна пропасть из-за секундной занятости шины.
            Q_EMIT errorOccurred(tr("Could not send to node %1: %2").arg(m_node).arg(detail));
            return;
        }

        m_pending.remove(0, chunk.size());
        m_lastFrameSentMs = now;
    }
}

void CliCanChannel::handleFrame(quint32 id, const QByteArray &data, qint64 monotonicNs)
{
    if (m_state != ChannelState::Open)
        return;

    if (const int pauseMs = clican::flowControlPauseMs(id, data); pauseMs >= 0) {
        // Просьба шлюза придержать отправку: его очередь приёма близка к переполнению.
        m_pausedUntilMs = m_clock.elapsed() + pauseMs;
        return;
    }

    if (id != clican::responseId(m_node))
        return;

    m_lastResponseMs = m_clock.elapsed();
    if (m_silenceReported) {
        m_silenceReported = false;
        setState(ChannelState::Open, tr("Node %1").arg(m_node));
    }

    // Пустой пакет — подтверждение присутствия, а не данные: в терминал ему попадать не за
    // чем, иначе каждое удержание туннеля порождало бы пустую строку.
    if (!data.isEmpty())
        Q_EMIT dataReceived(data, monotonicNs);
}

void CliCanChannel::tick()
{
    if (!m_bus || m_state != ChannelState::Open)
        return;

    const qint64 now = m_clock.elapsed();

    drainQueue();

    // Удержание туннеля: любой пакет на приёмный адрес продлевает режим, поэтому считать
    // нужно от последнего отправленного, а не от последнего пустого.
    if (now - m_lastFrameSentMs >= m_keepAliveMs && now >= m_pausedUntilMs) {
        QString detail;
        if (m_bus->send(clican::requestId(m_node), QByteArray(), &detail))
            m_lastFrameSentMs = now;
    }

    // Молчание узла — не повод закрывать канал: плату могли перезагрузить, и она вернётся.
    // Но и молчать об этом нельзя, иначе пустой терминал выглядит как «программа не
    // работает». Состояние остаётся Open — писать в него по-прежнему можно.
    const qint64 silentFor = now - qMax(m_lastResponseMs, qint64(0));
    if (!m_silenceReported && m_responseTimeoutMs > 0 && silentFor > m_responseTimeoutMs) {
        m_silenceReported = true;
        Q_EMIT errorOccurred(tr("Node %1 has not answered for %2 ms")
                                 .arg(m_node)
                                 .arg(m_responseTimeoutMs));
        setState(ChannelState::Open, tr("Node %1 - no answer").arg(m_node));
    }
}

void CliCanChannel::setState(ChannelState state, const QString &detail)
{
    if (m_state == state && detail.isEmpty())
        return;
    m_state = state;
    Q_EMIT stateChanged(state, detail);
}

} // namespace spotty
