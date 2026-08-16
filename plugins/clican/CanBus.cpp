/**
 * \file CanBus.cpp
 * \brief Реализация spotty::CanBus и spotty::CanBusPool.
 */
#include "CanBus.h"

#include <QLoggingCategory>
#include <QMutexLocker>
#include <QObject>

#include <chrono>
#include <utility>

#if defined(Q_OS_WIN)
#    include <windows.h>
#else
#    include <sys/select.h>
#    include <unistd.h>
#endif

namespace spotty {

namespace {

Q_LOGGING_CATEGORY(lcCanBus, "spotty.plugins.clican")

/// \brief Насколько поток приёма засыпает, не дождавшись кадров.
///
/// Определяет не задержку данных (её задаёт событие драйвера), а лишь то, как быстро поток
/// заметит просьбу остановиться и наступивший срок широковещательного запроса.
constexpr int kWaitTimeoutMs = 50;

/// \brief Сон запасного пути, когда драйвер не отдаёт события приёма.
constexpr int kPollSleepMs = 1;

} // namespace

CanBus::CanBus(PcanLibrary::Handle handle)
    : m_handle(handle)
{
}

CanBus::~CanBus()
{
    close();
}

bool CanBus::open(int bitrate, QString *error)
{
    if (m_open) {
        if (m_bitrate == bitrate)
            return true;
        // Скорость у канала одна на всех: сменить её, не разорвав шину, нельзя.
        qCInfo(lcCanBus) << "reopening PCAN channel" << m_handle << "at" << bitrate << "bps";
        close();
    }

    const quint16 code = PcanLibrary::btrCode(bitrate);
    if (code == 0) {
        if (error)
            *error = QObject::tr("Unsupported CAN bit rate: %1 bit/s").arg(bitrate);
        return false;
    }

    QString detail;
    if (!PcanLibrary::instance().initialize(m_handle, code, &detail)) {
        if (error)
            *error = QObject::tr("Could not open the CAN channel: %1").arg(detail);
        return false;
    }

    m_bitrate = bitrate;
    m_clock.start();
    {
        const QMutexLocker locker(&m_nodesMutex);
        m_nodes.clear();
    }

    m_receiveEvent = -1;
    m_ownsReceiveEvent = false;
#if defined(Q_OS_WIN)
    // На Windows событие приёма создаёт приложение и отдаёт драйверу; на POSIX наоборот —
    // драйвер возвращает готовый дескриптор. Обе ветки заканчиваются одним и тем же:
    // значением, на котором умеет ждать waitForFrames().
    if (HANDLE event = CreateEventA(nullptr, FALSE, FALSE, nullptr)) {
        if (PcanLibrary::instance().setReceiveEvent(m_handle,
                                                    reinterpret_cast<qintptr>(event))) {
            m_receiveEvent = reinterpret_cast<qintptr>(event);
            m_ownsReceiveEvent = true;
        } else {
            CloseHandle(event);
        }
    }
#elif defined(Q_OS_MACOS)
    // На libPCBUSB (macCAN, единственный драйвер PCAN для macOS) PCAN_RECEIVE_EVENT ложно
    // сигналит готовность: следующий CAN_Read() блокируется где-то внутри драйвера в
    // обычном блокирующем read(), ожидая кадр, который может не прийти никогда — а наши же
    // широковещательные запросы шлёт тот же поток, который теперь заблокирован, так что он
    // не придёт и подавно. Хуже того, CAN_Uninitialize() из другого потока не может
    // прервать зависший CAN_Read(): оба ждут один и тот же внутренний мьютекс драйвера
    // (видно по стеку pcan_usb_can_close() и pcan_usb_can_read()), поэтому close() тоже
    // виснет намертво. Событие не запрашивается вовсе — сразу обычный опрос: проверено на
    // PCAN-USB и libPCBUSB 4.9.0.1, событие вешало поток за пару секунд без единого
    // ответившего узла, опрос ни разу не завис и вдобавок нашёл узел, которого событие не
    // показывало.
#else
    m_receiveEvent = PcanLibrary::instance().receiveEvent(m_handle);
#endif
    if (m_receiveEvent < 0) {
        qCInfo(lcCanBus) << "PCAN receive event is unavailable; falling back to polling";
    }

    m_open = true;
    m_stopping = false;
    m_reader = std::thread([this] { readLoop(); });
    return true;
}

void CanBus::close()
{
    if (!m_open)
        return;

    // Поток останавливается до CAN_Uninitialize(): иначе он ушёл бы читать из уже закрытой
    // ручки. Ждать долго не приходится — ожидание кадров само просыпается раз в 50 мс.
    m_stopping = true;
    if (m_reader.joinable())
        m_reader.join();

    PcanLibrary::instance().uninitialize(m_handle);

#if defined(Q_OS_WIN)
    if (m_ownsReceiveEvent && m_receiveEvent >= 0)
        CloseHandle(reinterpret_cast<HANDLE>(m_receiveEvent));
#endif
    m_receiveEvent = -1;
    m_ownsReceiveEvent = false;

    m_open = false;
    m_bitrate = 0;

    const QMutexLocker locker(&m_nodesMutex);
    m_nodes.clear();
}

bool CanBus::send(quint32 id, const QByteArray &payload, QString *error)
{
    if (!m_open) {
        if (error)
            *error = QObject::tr("The CAN channel is closed");
        return false;
    }

    const QMutexLocker locker(&m_txMutex);
    return PcanLibrary::instance().write(m_handle, id, payload, error);
}

void CanBus::setDiscoveryEnabled(bool enabled)
{
    m_discovery = enabled;
}

QList<int> CanBus::nodes() const
{
    const QMutexLocker locker(&m_nodesMutex);
    return m_nodes.nodes(m_clock.isValid() ? m_clock.elapsed() : 0, kNodeExpiryMs);
}

int CanBus::addHandler(FrameHandler frameHandler, ErrorHandler errorHandler)
{
    const QMutexLocker locker(&m_handlerMutex);
    const int token = m_nextToken++;
    m_handlers.append(Subscriber{token, std::move(frameHandler), std::move(errorHandler)});
    return token;
}

void CanBus::removeHandler(int token)
{
    // Мьютекс держится и на время вызова обработчиков (см. dispatch()), поэтому возврат
    // отсюда означает, что обработчик подписчика больше не выполняется и не начнётся.
    const QMutexLocker locker(&m_handlerMutex);
    m_handlers.removeIf([token](const Subscriber &s) { return s.token == token; });
}

void CanBus::readLoop()
{
    qint64 lastDiscovery = -kDiscoveryIntervalMs;

    while (!m_stopping) {
        waitForFrames();
        if (m_stopping)
            break;

        drainQueue();

        if (m_discovery) {
            const qint64 now = m_clock.elapsed();
            if (now - lastDiscovery >= kDiscoveryIntervalMs) {
                lastDiscovery = now;
                // Пустой широковещательный пакет: каждый узел обязан отозваться пустым
                // пакетом со своего идентификатора ответа, и это единственный способ
                // узнать, кто на шине. Ошибку отправки здесь не показываем — на шине без
                // единого узла её причина «никто не подтвердил приём», и жаловаться на неё
                // раз в полсекунды значило бы завалить пользователя сообщениями.
                send(clican::kBroadcastId, QByteArray(), nullptr);
            }
        }
    }
}

void CanBus::waitForFrames()
{
    if (m_receiveEvent < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollSleepMs));
        return;
    }

#if defined(Q_OS_WIN)
    WaitForSingleObject(reinterpret_cast<HANDLE>(m_receiveEvent), kWaitTimeoutMs);
#else
    const int fd = static_cast<int>(m_receiveEvent);
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(fd, &readable);

    timeval timeout = {};
    timeout.tv_usec = kWaitTimeoutMs * 1000;

    if (select(fd + 1, &readable, nullptr, nullptr, &timeout) > 0 && FD_ISSET(fd, &readable)) {
        // Драйвер сигналит записью в канал; не вычитав её, мы будили бы поток вхолостую
        // до конца жизни шины.
        quint64 token = 0;
        ::read(fd, &token, sizeof(token));
    }
#endif
}

void CanBus::drainQueue()
{
    PcanLibrary &library = PcanLibrary::instance();

    // Очередь выгребается целиком: одно пробуждение может нести десятки кадров, а
    // возвращаться за каждым в ожидание значило бы платить системным вызовом за кадр.
    for (;;) {
        PcanLibrary::Frame frame;
        bool empty = false;
        QString error;
        if (!library.read(m_handle, &frame, &empty, &error)) {
            if (empty)
                return;
            reportError(QObject::tr("CAN bus error: %1").arg(error));
            return;
        }

        // Отметка времени — здесь, в момент чтения: из неё ядро считает межбайтовые паузы
        // для пакетизации, и задержка на обработку исказила бы разбиение потока на кадры.
        dispatch(frame.id, frame.data, m_clock.nsecsElapsed());

        if (m_stopping)
            return;
    }
}

void CanBus::dispatch(quint32 id, const QByteArray &data, qint64 monotonicNs)
{
    if (const int node = clican::nodeFromResponseId(id)) {
        const QMutexLocker locker(&m_nodesMutex);
        m_nodes.noteSeen(node, m_clock.elapsed());
    }

    const QMutexLocker locker(&m_handlerMutex);
    for (const Subscriber &subscriber : std::as_const(m_handlers)) {
        if (subscriber.onFrame)
            subscriber.onFrame(id, data, monotonicNs);
    }
}

void CanBus::reportError(const QString &message)
{
    const QMutexLocker locker(&m_handlerMutex);
    for (const Subscriber &subscriber : std::as_const(m_handlers)) {
        if (subscriber.onError)
            subscriber.onError(message);
    }
}

std::shared_ptr<CanBus> CanBusPool::acquire(PcanLibrary::Handle handle, int bitrate,
                                            QString *error)
{
    const QMutexLocker locker(&m_mutex);

    Entry &entry = m_entries[handle];
    if (!entry.bus)
        entry.bus = std::make_unique<CanBus>(handle);

    if (!entry.bus->open(bitrate, error)) {
        // Неудачное открытие не должно оставлять за собой запись: следующая попытка
        // (пользователь выбрал другую скорость) обязана начаться с чистого места.
        if (entry.users == 0)
            m_entries.erase(handle);
        return {};
    }

    ++entry.users;

    // Указатель без владения: сама шина принадлежит пулу, а удалитель лишь сообщает ему,
    // что владельцем стало на одного меньше. Захват `this`, а не обращение к общему
    // синглтону — держатель shared_ptr обязан отпустить его раньше, чем разрушится сам
    // пул (см. \par «Почему не синглтон» в CanBus.h), и это гарантируется владением: пул
    // живёт членом spotty::CliCanPlugin, а держатели — им самим (m_scanBus) и его же
    // каналами (spotty::CliCanChannel), которые не переживают свой плагин.
    return std::shared_ptr<CanBus>(entry.bus.get(), [this, handle](CanBus *) {
        release(handle);
    });
}

void CanBusPool::release(PcanLibrary::Handle handle)
{
    const QMutexLocker locker(&m_mutex);

    const auto it = m_entries.find(handle);
    if (it == m_entries.end())
        return;

    if (--it->second.users > 0)
        return;

    // Закрытие и разрушение — под тем же мьютексом, что и открытие: иначе acquire() из
    // другого потока успел бы открыть канал между этими двумя строками, а закрытие
    // отняло бы его обратно.
    m_entries.erase(it);
}

} // namespace spotty
