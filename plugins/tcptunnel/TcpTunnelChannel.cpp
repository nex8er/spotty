/**
 * \file TcpTunnelChannel.cpp
 * \brief Реализация spotty::TcpTunnelChannel.
 */
#include "TcpTunnelChannel.h"

#include "TcpTunnelSettings.h"

#include <QTcpSocket>
#include <QTimer>

#include <utility>

namespace spotty {

namespace {

/// \brief Ожидание соединения, если в настройках нуль или мусор, мс.
constexpr int kDefaultConnectTimeoutMs = 3000;

/// \brief Сколько ждать отправки хвоста при закрытии, мс.
constexpr int kCloseFlushMs = 200;

} // namespace

TcpTunnelChannel::TcpTunnelChannel(QObject *parent)
    : IInterfaceChannel(parent)
{
    // Сокет и таймеры создаются в open(): к этому моменту канал уже перенесён в поток
    // ввода-вывода, и объекты с таймерами или сокетами, созданные раньше, привязались бы к
    // потоку создателя.
}

TcpTunnelChannel::~TcpTunnelChannel()
{
    teardown();
}

bool TcpTunnelChannel::open(const QVariantMap &settings, QString *error)
{
    if (m_state == ChannelState::Open)
        return true;

    using namespace tcptunnel;
    setState(ChannelState::Opening);

    m_host = settings.value(QLatin1String(kHost)).toString().trimmed();
    // Через int, а не сразу в quint16: 70000 тихо превратилось бы в 4464, и подключение пошло
    // бы на чужой порт. Редактируемый список порта числа за пределами диапазона пропускает.
    const int port = settings.value(QLatin1String(kPort), kDefaultPort).toInt();
    m_port = (port >= 1 && port <= 65535) ? quint16(port) : quint16(0);
    m_autoReconnect = settings.value(QLatin1String(kAutoReconnect), true).toBool();
    int timeoutMs = settings.value(QLatin1String(kConnectTimeoutMs)).toInt();
    if (timeoutMs <= 0)
        timeoutMs = kDefaultConnectTimeoutMs;

    // Схема не пускает сюда пустой адрес (поле обязательное), но канал можно открыть и в
    // обход схемы — из теста, из чужого кода, — и тогда понятная причина лучше, чем
    // «Host not found» на пустое имя.
    QString problem;
    if (m_host.isEmpty())
        problem = tr("The host is not set.");
    else if (m_port == 0)
        problem = tr("Invalid port.");
    if (!problem.isEmpty())
        return failOpen(problem, error);

    m_clock.start();
    m_reconnecting = false;

    m_socket = new QTcpSocket(this);
    // Без Нейгла: терминалу нужен каждый введённый байт сразу, а не через 40 мс. Проверка
    // живости соединения нужна, потому что вынутый кабель не присылает FIN, и без неё
    // оборванное соединение выглядело бы открытым бесконечно.
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &TcpTunnelChannel::attemptReconnect);

    m_settleTimer = new QTimer(this);
    m_settleTimer->setSingleShot(true);
    connect(m_settleTimer, &QTimer::timeout, this, [this] {
        if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
            return;
        m_reconnecting = false;
        setState(ChannelState::Open);
    });

    connect(m_socket, &QTcpSocket::readyRead, this, &TcpTunnelChannel::onReadyRead);
    connect(m_socket, &QTcpSocket::connected, this, &TcpTunnelChannel::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TcpTunnelChannel::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        // Во время переподключения неудачная попытка не даёт сигнала disconnected (соединения
        // и не было), и очередную пришлось бы ждать вечно, не взведи мы её здесь.
        if (m_reconnecting && !m_retryTimer->isActive())
            m_retryTimer->start(kRetryMs);
    });

    m_socket->connectToHost(m_host, m_port);
    if (!m_socket->waitForConnected(timeoutMs)) {
        const QString reason = m_socket->error() == QAbstractSocket::SocketTimeoutError
                                   ? tr("timed out")
                                   : m_socket->errorString();
        return failOpen(tr("Cannot connect to %1:%2: %3").arg(m_host).arg(m_port).arg(reason), error);
    }

    // Сервер мог принять и тут же закрыть соединение, пока мы возвращались из ожидания:
    // обработчик обрыва ещё не подключён к делу (состояние Opening), и «открыто» было бы ложью.
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return failOpen(tr("Connection to %1:%2 was closed right away.").arg(m_host).arg(m_port), error);

    setState(ChannelState::Open);

    // Данные могли прийти, пока шло ожидание: сигнал readyRead в это время испускается
    // синхронно, до того как мы успели бы сюда дойти, и повторно его не будет.
    if (m_socket->bytesAvailable() > 0)
        onReadyRead();
    return true;
}

bool TcpTunnelChannel::failOpen(const QString &message, QString *error)
{
    teardown();
    if (error)
        *error = message;
    setState(ChannelState::Error, message);
    return false;
}

void TcpTunnelChannel::close()
{
    teardown();
    setState(ChannelState::Closed);
}

qint64 TcpTunnelChannel::write(const QByteArray &data)
{
    // Пока идёт переподключение, писать некуда; тихо принять и потерять нельзя —
    // пользователь решил бы, что команда дошла.
    if (!m_socket || m_state != ChannelState::Open
        || m_socket->state() != QAbstractSocket::ConnectedState) {
        return -1;
    }
    return m_socket->write(data);
}

void TcpTunnelChannel::onReadyRead()
{
    if (!m_socket)
        return;
    // Метка ставится в момент чтения, а не позже: из неё считаются межбайтовые паузы
    // пакетизации (см. IInterfaceChannel::dataReceived()).
    const qint64 timestamp = m_clock.nsecsElapsed();
    const QByteArray chunk = m_socket->readAll();
    if (!chunk.isEmpty())
        Q_EMIT dataReceived(chunk, timestamp);
}

void TcpTunnelChannel::onConnected()
{
    // Первое открытие подтверждается самим open(); здесь важен только возврат после обрыва.
    if (m_reconnecting)
        m_settleTimer->start(kSettleMs);
}

void TcpTunnelChannel::onDisconnected()
{
    if (!m_socket || m_state == ChannelState::Closed || m_state == ChannelState::Error)
        return;

    m_settleTimer->stop();

    if (m_autoReconnect) {
        m_reconnecting = true;
        setState(ChannelState::Unavailable,
                 tr("Connection to %1:%2 lost, reconnecting…").arg(m_host).arg(m_port));
        if (!m_retryTimer->isActive())
            m_retryTimer->start(kRetryMs);
        return;
    }

    teardown();
    const QString message =
        tr("Connection closed by %1:%2. If the agent is running, check that the serial port "
           "is connected on the remote machine.")
            .arg(m_host)
            .arg(m_port);
    setState(ChannelState::Error, message);
    Q_EMIT errorOccurred(message);
}

void TcpTunnelChannel::attemptReconnect()
{
    if (!m_socket)
        return;
    m_socket->abort();
    m_socket->connectToHost(m_host, m_port);
}

void TcpTunnelChannel::teardown()
{
    // Таймеры — потомки канала и сами ничего не вызовут после удаления.
    delete std::exchange(m_retryTimer, nullptr);
    delete std::exchange(m_settleTimer, nullptr);
    if (m_socket) {
        // Отключаем обработчики до закрытия: иначе disconnected от собственного закрытия
        // принялся бы за обрыв и запустил переподключение.
        m_socket->disconnect(this);
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->flush();
            m_socket->disconnectFromHost();
            if (m_socket->state() != QAbstractSocket::UnconnectedState)
                m_socket->waitForDisconnected(kCloseFlushMs);
        }
        m_socket->abort();
        // deleteLater(), а не delete: teardown() зовётся и из обработчика сигнала самого
        // сокета (disconnected без автопереподключения), и удаление объекта изнутри его
        // собственного сигнала оставляет Qt обращаться к освобождённой памяти. Сокет —
        // потомок канала, поэтому отложенное удаление не переживёт и его самого.
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_reconnecting = false;
}

void TcpTunnelChannel::setState(ChannelState state, const QString &detail)
{
    if (m_state == state && detail.isEmpty())
        return;
    m_state = state;
    Q_EMIT stateChanged(m_state, detail);
}

} // namespace spotty
