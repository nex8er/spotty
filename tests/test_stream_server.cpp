/**
 * \file test_stream_server.cpp
 * \brief Тесты сервера потока и его панели: spotty::StreamServer, spotty::StreamServerPanel.
 */
#include "StreamServer.h"
#include "StreamServerPanel.h"
#include "StreamServerPlugin.h"

#include "support/FakePanelHost.h"
#include "support/TestSupport.h"

#include <QFile>
#include <QLocalSocket>
#include <QTcpSocket>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::TempDir;
using spotty::test::waitFor;

namespace {

StreamServer::Config tcpConfig()
{
    StreamServer::Config config;
    config.kind = StreamServer::Kind::Tcp;
    config.address = QHostAddress::LocalHost;
    config.port = 0; // свободный: тест не должен зависеть от того, что занято на машине
    return config;
}

StreamServer::Config localConfig(const QString &path)
{
    StreamServer::Config config;
    config.kind = StreamServer::Kind::Local;
    config.localName = path;
    return config;
}

/// \brief Подключиться по TCP и дождаться, пока сервер увидит клиента.
std::unique_ptr<QTcpSocket> connectTcp(StreamServer &server, int expectedClients)
{
    auto socket = std::make_unique<QTcpSocket>();
    socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
    EXPECT_TRUE(socket->waitForConnected(2000));
    EXPECT_TRUE(waitFor([&] { return server.clientCount() == expectedClients; }));
    return socket;
}

std::unique_ptr<QLocalSocket> connectLocal(StreamServer &server, const QString &path,
                                           int expectedClients)
{
    auto socket = std::make_unique<QLocalSocket>();
    socket->connectToServer(path);
    EXPECT_TRUE(socket->waitForConnected(2000));
    EXPECT_TRUE(waitFor([&] { return server.clientCount() == expectedClients; }));
    return socket;
}

/// \brief Все байты 0..255 подряд: проверка, что поток не трогают ни нули, ни переводы строк.
QByteArray everyByte()
{
    QByteArray data;
    for (int i = 0; i < 256; ++i)
        data.append(char(i));
    return data;
}

} // namespace

TEST(StreamServer, TcpBroadcastReachesEveryClientAsIs)
{
    StreamServer server;
    QString error;
    ASSERT_TRUE(server.start(tcpConfig(), &error)) << qPrintable(error);
    ASSERT_NE(server.serverPort(), 0);

    auto first = connectTcp(server, 1);
    auto second = connectTcp(server, 2);

    server.broadcast(everyByte());

    ASSERT_TRUE(waitFor([&] { return first->bytesAvailable() >= 256; }));
    ASSERT_TRUE(waitFor([&] { return second->bytesAvailable() >= 256; }));
    EXPECT_EQ(first->readAll(), everyByte());
    EXPECT_EQ(second->readAll(), everyByte());
}

TEST(StreamServer, ClientBytesArriveUnchanged)
{
    StreamServer server;
    QString error;
    ASSERT_TRUE(server.start(tcpConfig(), &error)) << qPrintable(error);

    QByteArray received;
    QObject::connect(&server, &StreamServer::dataFromClients,
                     [&](const QByteArray &data) { received.append(data); });

    auto client = connectTcp(server, 1);
    client->write(everyByte());
    client->flush();

    ASSERT_TRUE(waitFor([&] { return received.size() >= 256; }));
    EXPECT_EQ(received, everyByte());
}

TEST(StreamServer, LocalSocketWorksBothWays)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("s.sock"));

    StreamServer server;
    QString error;
    ASSERT_TRUE(server.start(localConfig(path), &error)) << qPrintable(error);

    QByteArray received;
    QObject::connect(&server, &StreamServer::dataFromClients,
                     [&](const QByteArray &data) { received.append(data); });

    auto client = connectLocal(server, path, 1);

    server.broadcast(QByteArrayLiteral("from device\r\n"));
    ASSERT_TRUE(waitFor([&] { return client->bytesAvailable() >= 13; }));
    EXPECT_EQ(client->readAll(), QByteArrayLiteral("from device\r\n"));

    client->write(QByteArrayLiteral("AT\r"));
    client->flush();
    ASSERT_TRUE(waitFor([&] { return received == QByteArrayLiteral("AT\r"); }));
}

TEST(StreamServer, ReplacesAStaleSocketFileButNotALiveServer)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("s.sock"));

#ifndef Q_OS_WIN
    // Осиротевший файл: сервер был, процесс умер, файл остался. На Windows у имени канала
    // файла нет, поэтому и проверять там нечего.
    QFile leftover(path);
    ASSERT_TRUE(leftover.open(QIODevice::WriteOnly));
    leftover.close();
#endif

    StreamServer first;
    QString error;
    ASSERT_TRUE(first.start(localConfig(path), &error)) << qPrintable(error);

    // Живой сервер на этом имени второму отдавать нельзя: иначе подключения делились бы
    // между двумя, и ни один из них не получил бы поток целиком.
    StreamServer second;
    EXPECT_FALSE(second.start(localConfig(path), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(first.isListening());
}

TEST(StreamServer, CountsClientsAsTheyComeAndGo)
{
    StreamServer server;
    QString error;
    ASSERT_TRUE(server.start(tcpConfig(), &error)) << qPrintable(error);

    auto first = connectTcp(server, 1);
    auto second = connectTcp(server, 2);

    first->disconnectFromHost();
    first.reset();
    ASSERT_TRUE(waitFor([&] { return server.clientCount() == 1; }));
}

TEST(StreamServer, StopDisconnectsClientsAndFreesTheSocketFile)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("s.sock"));

    StreamServer server;
    QString error;
    ASSERT_TRUE(server.start(localConfig(path), &error)) << qPrintable(error);
    auto client = connectLocal(server, path, 1);

    server.stop();
    EXPECT_FALSE(server.isListening());
    EXPECT_EQ(server.clientCount(), 0);
    EXPECT_FALSE(QFile::exists(path));
    EXPECT_TRUE(waitFor([&] { return client->state() == QLocalSocket::UnconnectedState; }));
}

TEST(StreamServer, DropsAClientThatDoesNotRead)
{
    StreamServer server;
    QString error;
    ASSERT_TRUE(server.start(tcpConfig(), &error)) << qPrintable(error);

    int dropped = 0;
    QObject::connect(&server, &StreamServer::clientDropped, [&](const QString &) { ++dropped; });

    auto stuck = connectTcp(server, 1);

    // События не обрабатываются, поэтому всё, что мы пишем, остаётся в буфере сервера.
    const QByteArray chunk(1024 * 1024, 'x');
    for (int i = 0; i < 12 && server.clientCount() > 0; ++i)
        server.broadcast(chunk);

    EXPECT_EQ(dropped, 1);
    EXPECT_EQ(server.clientCount(), 0);
    EXPECT_TRUE(server.isListening()); // сервер жив: потерян только один клиент
}

TEST(StreamServer, RefusesAnEmptySocketName)
{
    StreamServer server;
    QString error;
    EXPECT_FALSE(server.start(localConfig(QString()), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(server.isListening());
}

namespace {

/// \brief Панель на локальном сокете во временном каталоге — без борьбы за порты.
struct PanelFixture
{
    test::TempDir dir;
    test::FakePanelHost host{dir.path()};
    StreamServerPlugin plugin;
    StreamServerPanel *panel = nullptr;
    QString socketPath;

    explicit PanelFixture(bool includeTx = false)
    {
        socketPath = dir.filePath(QStringLiteral("panel.sock"));
        host.settings[QStringLiteral("kind")] = int(StreamServer::Kind::Local);
        host.settings[QStringLiteral("localName")] = socketPath;
        host.settings[QStringLiteral("includeTx")] = includeTx;
        host.channelStateValue = ChannelState::Open;
        panel = qobject_cast<StreamServerPanel *>(
            plugin.createPanel(QStringLiteral("streamserver"), &host, nullptr));
    }
    ~PanelFixture() { delete panel; }
};

} // namespace

TEST(StreamServerPanel, PassesDeviceDataToClientsAndIgnoresItWhileStopped)
{
    PanelFixture fixture;
    ASSERT_NE(fixture.panel, nullptr);

    // Остановлен: данные никому не нужны и никуда не идут.
    Q_EMIT fixture.host.dataLogged(QByteArrayLiteral("early"), DataDirection::Rx);

    ASSERT_TRUE(fixture.panel->startServer());
    auto client = connectLocal(*fixture.panel->server(), fixture.socketPath, 1);

    Q_EMIT fixture.host.dataLogged(QByteArrayLiteral("hello"), DataDirection::Rx);
    ASSERT_TRUE(waitFor([&] { return client->bytesAvailable() >= 5; }));
    EXPECT_EQ(client->readAll(), QByteArrayLiteral("hello"));
}

TEST(StreamServerPanel, SentDataIsNotMixedInUnlessAsked)
{
    PanelFixture fixture;
    ASSERT_TRUE(fixture.panel->startServer());
    auto client = connectLocal(*fixture.panel->server(), fixture.socketPath, 1);

    // Отправленное клиентом возвращается в dataLogged() как Tx. Без этого правила скрипт
    // получал бы собственные байты обратно, будто их прислало устройство.
    Q_EMIT fixture.host.dataLogged(QByteArrayLiteral("mine"), DataDirection::Tx);
    Q_EMIT fixture.host.dataLogged(QByteArrayLiteral("theirs"), DataDirection::Rx);

    ASSERT_TRUE(waitFor([&] { return client->bytesAvailable() >= 6; }));
    EXPECT_EQ(client->readAll(), QByteArrayLiteral("theirs"));
}

TEST(StreamServerPanel, SentDataIsPassedOnWhenRequested)
{
    PanelFixture fixture(/*includeTx=*/true);
    ASSERT_TRUE(fixture.panel->startServer());
    auto client = connectLocal(*fixture.panel->server(), fixture.socketPath, 1);

    Q_EMIT fixture.host.dataLogged(QByteArrayLiteral("tx"), DataDirection::Tx);
    ASSERT_TRUE(waitFor([&] { return client->bytesAvailable() >= 2; }));
    EXPECT_EQ(client->readAll(), QByteArrayLiteral("tx"));
}

TEST(StreamServerPanel, ClientWritesGoToTheInterfaceAsIs)
{
    PanelFixture fixture;
    ASSERT_TRUE(fixture.panel->startServer());
    auto client = connectLocal(*fixture.panel->server(), fixture.socketPath, 1);

    client->write(everyByte());
    client->flush();

    ASSERT_TRUE(waitFor([&] { return fixture.host.lastSent.size() >= 256; }));
    EXPECT_EQ(fixture.host.lastSent, everyByte());
}

TEST(StreamServerPanel, ClientWritesAreNotSentIntoAClosedInterface)
{
    PanelFixture fixture;
    fixture.host.channelStateValue = ChannelState::Closed;
    ASSERT_TRUE(fixture.panel->startServer());
    auto client = connectLocal(*fixture.panel->server(), fixture.socketPath, 1);

    QByteArray seen;
    QObject::connect(fixture.panel->server(), &StreamServer::dataFromClients,
                     [&](const QByteArray &data) { seen.append(data); });
    client->write(QByteArrayLiteral("lost"));
    client->flush();

    ASSERT_TRUE(waitFor([&] { return seen == QByteArrayLiteral("lost"); }));
    EXPECT_EQ(fixture.host.sendCalls, 0);
}

TEST(StreamServerPanel, StopsOnCloseSoTheSocketFileDoesNotLinger)
{
    PanelFixture fixture;
    ASSERT_TRUE(fixture.panel->startServer());
    ASSERT_TRUE(QFile::exists(fixture.socketPath));

    Q_EMIT fixture.host.aboutToClose();
    EXPECT_FALSE(fixture.panel->server()->isListening());
    EXPECT_FALSE(QFile::exists(fixture.socketPath));
}

TEST(StreamServerPanel, StartsByItselfWhenAskedTo)
{
    TempDir dir;
    test::FakePanelHost host{dir.path()};
    host.settings[QStringLiteral("kind")] = int(StreamServer::Kind::Local);
    host.settings[QStringLiteral("localName")] = dir.filePath(QStringLiteral("auto.sock"));
    host.settings[QStringLiteral("autoStart")] = true;

    StreamServerPlugin plugin;
    std::unique_ptr<QWidget> panel(
        plugin.createPanel(QStringLiteral("streamserver"), &host, nullptr));
    auto *typed = qobject_cast<StreamServerPanel *>(panel.get());
    ASSERT_NE(typed, nullptr);
    EXPECT_TRUE(typed->server()->isListening());
}
