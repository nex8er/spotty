/**
 * \file StreamServer.cpp
 * \brief Реализация spotty::StreamServer.
 */
#include "StreamServer.h"

#include <QAbstractSocket>
#include <QLocalServer>
#include <QLocalSocket>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>

#include <type_traits>

namespace spotty {

StreamServer::StreamServer(QObject *parent)
    : QObject(parent)
{
}

StreamServer::~StreamServer()
{
    stop();
}

bool StreamServer::start(const Config &config, QString *error)
{
    stop();
    m_config = config;
    m_bytesToClients = 0;
    m_bytesFromClients = 0;

    const auto fail = [this, error](const QString &message) {
        if (error)
            *error = message;
        stop();
        return false;
    };

    if (config.kind == Kind::Local) {
        if (config.localName.trimmed().isEmpty())
            return fail(tr("Enter the socket path."));

        // Сначала выяснить, не слушает ли это имя живой сервер. Убрать «осиротевший» файл
        // сокета после аварийного выхода нужно, а затереть чужой работающий нельзя: на
        // Windows listen() вообще не отказывает, если имя занято (Qt документирует это), и
        // два сервера молча делили бы подключения.
        QLocalSocket probe;
        probe.connectToServer(config.localName);
        if (probe.waitForConnected(150)) {
            probe.abort();
            return fail(tr("%1 is already used by another program.").arg(config.localName));
        }
        QLocalServer::removeServer(config.localName);

        m_local = new QLocalServer(this);
        // Только владелец: сокет без авторизации, и открытым для всех пользователей
        // компьютера его делать незачем — для них есть TCP.
        m_local->setSocketOptions(QLocalServer::UserAccessOption);
        connect(m_local, &QLocalServer::newConnection, this, &StreamServer::acceptLocal);
        if (!m_local->listen(config.localName))
            return fail(m_local->errorString());
        return true;
    }

    m_tcp = new QTcpServer(this);
    connect(m_tcp, &QTcpServer::newConnection, this, &StreamServer::acceptTcp);
    if (!m_tcp->listen(config.address, config.port))
        return fail(m_tcp->errorString());
    return true;
}

void StreamServer::stop()
{
    const bool hadClients = !m_clients.isEmpty();

    // Копия: отключение клиента меняет список через слот disconnected().
    const QList<Client> clients = m_clients;
    for (const Client &client : clients) {
        client.device->disconnect(this);
        client.abort();
        client.device->deleteLater();
    }
    m_clients.clear();

    if (m_local) {
        m_local->close(); // убирает и файл сокета
        delete m_local;
        m_local = nullptr;
    }
    if (m_tcp) {
        m_tcp->close();
        delete m_tcp;
        m_tcp = nullptr;
    }

    if (hadClients)
        Q_EMIT clientCountChanged(0);
}

bool StreamServer::isListening() const
{
    return (m_local && m_local->isListening()) || (m_tcp && m_tcp->isListening());
}

quint16 StreamServer::serverPort() const
{
    return m_tcp ? m_tcp->serverPort() : 0;
}

QString StreamServer::listenDescription() const
{
    if (m_local)
        return m_local->fullServerName();
    if (m_tcp) {
        const QString host = m_config.address == QHostAddress::Any
                                 ? tr("all interfaces")
                                 : m_config.address.toString();
        return QStringLiteral("%1:%2").arg(host).arg(m_tcp->serverPort());
    }
    return {};
}

QStringList StreamServer::reachableAddresses()
{
    QStringList result;
    const QList<QHostAddress> all = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : all) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
            result.append(address.toString());
    }
    return result;
}

template<class Socket>
void StreamServer::adopt(Socket *socket)
{
    if constexpr (std::is_same_v<Socket, QTcpSocket>) {
        // Без задержки Нейгла: терминальный обмен — короткие порции, и склеивать их ради
        // экономии пакетов значит задерживать каждую на десятки миллисекунд.
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    }

    m_clients.append({socket, [socket] { socket->abort(); }});

    connect(socket, &Socket::readyRead, this, [this, socket] {
        const QByteArray data = socket->readAll();
        if (data.isEmpty())
            return;
        m_bytesFromClients += data.size();
        Q_EMIT dataFromClients(data);
    });
    connect(socket, &Socket::disconnected, this, [this, socket] { removeClient(socket); });

    Q_EMIT clientCountChanged(clientCount());
}

void StreamServer::acceptLocal()
{
    while (m_local && m_local->hasPendingConnections())
        adopt(m_local->nextPendingConnection());
}

void StreamServer::acceptTcp()
{
    while (m_tcp && m_tcp->hasPendingConnections())
        adopt(m_tcp->nextPendingConnection());
}

void StreamServer::removeClient(QIODevice *device)
{
    for (qsizetype i = 0; i < m_clients.size(); ++i) {
        if (m_clients[i].device != device)
            continue;
        m_clients.removeAt(i);
        device->deleteLater();
        Q_EMIT clientCountChanged(clientCount());
        return;
    }
}

void StreamServer::broadcast(const QByteArray &data)
{
    if (data.isEmpty() || m_clients.isEmpty())
        return;

    // Копия списка: сброс медленного клиента меняет настоящий.
    const QList<Client> clients = m_clients;
    for (const Client &client : clients) {
        client.device->write(data);
        m_bytesToClients += data.size();

        if (client.device->bytesToWrite() > kMaxClientBacklog) {
            // abort(), а не close(): close() ждал бы, пока клиент прочитает накопленное, то
            // есть ровно того, чего он не делает.
            Q_EMIT clientDropped(
                tr("A client was disconnected: it does not read the stream fast enough."));
            client.device->disconnect(this);
            client.abort();
            removeClient(client.device);
        }
    }
}

} // namespace spotty
