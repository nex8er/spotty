/**
 * \file StreamServer.h
 * \brief Сервер, раздающий поток байт другим программам: локальный сокет или TCP.
 */
#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class QIODevice;
class QLocalServer;
class QTcpServer;

namespace spotty {

/**
 * \class StreamServer
 * \brief Принимает подключения и пересылает байты между клиентами и Spotty без разбора.
 *
 * \par Протокола нет
 *
 * Клиент получает то, что передали в broadcast(), ровно в том виде, а то, что клиент пишет,
 * уходит наружу сигналом dataFromClients() без изменений. Поэтому с сервером работают
 * `nc`, `socat` и `cat` в bash и обычный `socket` в Python, без библиотеки и без разбора
 * кадров. Платой за это служит отсутствие границ: направления и сообщения не различаются.
 *
 * \par Один медленный клиент не должен тормозить остальных
 *
 * Запись в сокет не блокирует — байты копятся в его буфере. Клиент, который не успевает
 * читать, иначе рос бы в памяти без предела, поэтому при превышении #kMaxClientBacklog он
 * отключается: остальные продолжают получать поток, а потерянный клиент может подключиться
 * заново.
 *
 * \note Класс живёт в потоке интерфейса, как и панель. Раздача идёт порциями, которые
 *       ядро уже собрало в пакеты (см. \c Session::processPendingIncoming), поэтому
 *       нагрузка на этот поток соответствует частоте пакетов, а не байтов.
 */
class StreamServer : public QObject
{
    Q_OBJECT

public:
    /// \brief Способ подключения клиентов.
    enum class Kind {
        Local, ///< Unix-сокет (путь) или именованный канал Windows (имя).
        Tcp,   ///< TCP-порт.
    };

    /// \brief Что слушать.
    struct Config
    {
        Kind kind = Kind::Local;

        /// \brief Путь сокета или имя канала для Kind::Local.
        QString localName;

        /// \brief Адрес для Kind::Tcp: `QHostAddress::LocalHost` либо `QHostAddress::Any`.
        QHostAddress address = QHostAddress::LocalHost;

        /// \brief Порт для Kind::Tcp; 0 — любой свободный (нужно тестам).
        quint16 port = 0;
    };

    /// \brief Сколько байт может накопиться у одного клиента, прежде чем он будет отключён.
    static constexpr qint64 kMaxClientBacklog = 8 * 1024 * 1024;

    explicit StreamServer(QObject *parent = nullptr);
    ~StreamServer() override;

    /**
     * \brief Начать слушать; уже слушающий сервер сначала останавливается.
     * \param error Причина отказа; может быть `nullptr`.
     */
    bool start(const Config &config, QString *error);

    /// \brief Закрыть сервер и отключить всех клиентов.
    void stop();

    bool isListening() const;

    /// \brief Куда подключаться: путь сокета либо `адрес:порт` с настоящим портом.
    QString listenDescription() const;

    /// \brief Порт TCP-сервера после start() — нужен, когда просили порт 0.
    quint16 serverPort() const;

    int clientCount() const { return int(m_clients.size()); }

    /**
     * \brief Адреса этого компьютера, по которым подключится удалённый клиент.
     *
     * Только IPv4 без loopback: их и вводят руками. Без них при «слушать на всех
     * интерфейсах» человеку пришлось бы узнавать адрес отдельно.
     */
    static QStringList reachableAddresses();

    /// \brief Отдать байты всем подключённым клиентам.
    void broadcast(const QByteArray &data);

    qint64 bytesToClients() const { return m_bytesToClients; }
    qint64 bytesFromClients() const { return m_bytesFromClients; }

Q_SIGNALS:
    /// \brief Клиент прислал байты. Приходят как есть, порциями, как их отдал сокет.
    void dataFromClients(const QByteArray &data);

    /// \brief Число клиентов изменилось.
    void clientCountChanged(int count);

    /// \brief Клиент отключён за то, что не читает: сообщение для строки состояния.
    void clientDropped(const QString &message);

private:
    struct Client
    {
        QIODevice *device = nullptr;
        std::function<void()> abort;
    };

    void acceptLocal();
    void acceptTcp();

    /// \brief Взять сокет под управление: подписаться и поставить в список.
    template<class Socket>
    void adopt(Socket *socket);

    void removeClient(QIODevice *device);

    QLocalServer *m_local = nullptr;
    QTcpServer *m_tcp = nullptr;
    QList<Client> m_clients;
    Config m_config;
    qint64 m_bytesToClients = 0;
    qint64 m_bytesFromClients = 0;
};

} // namespace spotty
