/**
 * \file SingleInstanceGuard.cpp
 * \brief Реализация spotty::SingleInstanceGuard.
 */
#include "SingleInstanceGuard.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>

namespace spotty {

/// \brief Категория журналирования: `spotty.instance`.
Q_LOGGING_CATEGORY(lcInstance, "spotty.instance")

namespace {

/// \brief Сколько ждать ответа работающего экземпляра, мс.
constexpr int kConnectTimeoutMs = 500;

/// \brief Сообщение, по которому работающий экземпляр показывает окно.
constexpr auto kRaiseMessage = "raise";

} // namespace

SingleInstanceGuard::SingleInstanceGuard(const QString &key, QObject *parent)
    : QObject(parent)
{
    // Имя пользователя подмешиваем через хеш: домашний каталог в открытом виде попал бы
    // в имя сокета, видимое другим пользователям системы.
    const QByteArray salt = QDir::homePath().toUtf8();
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(salt, QCryptographicHash::Sha1).toHex().left(12));

    m_serverName = QStringLiteral("%1-%2").arg(key, digest);
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    if (m_server) {
        m_server->close();
        QLocalServer::removeServer(m_serverName);
    }
}

bool SingleInstanceGuard::tryAcquire()
{
    if (m_server)
        return true;

    m_server = new QLocalServer(this);

    // Соединения обслуживаем ради самого факта: содержимое сообщения не важно, важно
    // лишь то, что кто-то попросил показать окно.
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            Q_EMIT raiseRequested();
        }
    });

    if (m_server->listen(m_serverName))
        return true;

    // Имя занято. Либо экземпляр действительно работает, либо предыдущий упал и оставил
    // после себя сокет. Различаем попыткой подключения.
    QLocalSocket probe;
    probe.connectToServer(m_serverName);
    if (probe.waitForConnected(kConnectTimeoutMs)) {
        probe.disconnectFromServer();
        delete m_server;
        m_server = nullptr;
        return false;
    }

    // Никто не ответил — сокет осиротел. Без этой уборки одно падение навсегда закрыло бы
    // возможность запустить программу.
    qCInfo(lcInstance) << "removing stale socket" << m_serverName;
    QLocalServer::removeServer(m_serverName);

    if (m_server->listen(m_serverName))
        return true;

    qCWarning(lcInstance) << "cannot listen on" << m_serverName << m_server->errorString();
    delete m_server;
    m_server = nullptr;

    // Занять имя не вышло по неизвестной причине. Запуститься всё равно лучше, чем
    // отказать пользователю в работе из-за служебной мелочи.
    return true;
}

bool SingleInstanceGuard::notifyExisting()
{
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(kConnectTimeoutMs))
        return false;

    socket.write(QByteArrayLiteral(kRaiseMessage));
    socket.waitForBytesWritten(kConnectTimeoutMs);
    socket.disconnectFromServer();
    return true;
}

} // namespace spotty
