/**
 * \file test_tcptunnel.cpp
 * \brief Тесты транспорта tcptunnel: spotty::TcpTunnelChannel и spotty::TcpTunnelPlugin.
 *
 * Канал проверяется на настоящем QTcpServer, без агента: агент проверяется своими тестами
 * (test_uartd.cpp), а здесь важна только сторона Spotty — состояния, обрыв и переподключение.
 */
#include "AgentPackage.h"
#include "NetworkScanner.h"
#include "TcpTunnelChannel.h"
#include "TcpTunnelEndpoint.h"
#include "TcpTunnelPlugin.h"

#include "support/TestSupport.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <settings/SettingsStore.h>

#include <QElapsedTimer>
#include <QSet>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <vector>

using namespace spotty;
using spotty::test::TempDir;
using spotty::test::waitFor;

namespace {

/**
 * \brief Порт, на котором заведомо никто не слушает.
 *
 * Порт 1 (tcpmux) — привилегированный и давно не используется. «Только что освобождённый»
 * эфемерный порт для этого не годится: ctest гоняет тесты параллельными процессами, и
 * чужой тест успевает занять его между закрытием сервера и проверкой.
 */
constexpr quint16 kClosedPort = 1;

/**
 * \brief Ожидание с запасом для тестов, где участвуют таймеры канала.
 *
 * Под параллельным ctest машина бывает загружена так, что обычные две секунды выходят раньше,
 * чем отработают таймер повтора (2 с) и выдержка (0.4 с).
 */
inline bool waitSlow(const std::function<bool()> &predicate, int timeoutMs = 8000)
{
    return waitFor(predicate, timeoutMs);
}

/// \brief Сервер на свободном порту петли, хранящий принятые соединения.
struct Server
{
    Server()
    {
        EXPECT_TRUE(server.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                ++accepted;
                if (dropEveryone) {
                    socket->disconnectFromHost();
                    socket->deleteLater();
                    continue;
                }
                socket->setParent(&server);
                clients.push_back(socket);
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [this, socket] { received.append(socket->readAll()); });
            }
        });
    }

    quint16 port() const { return server.serverPort(); }

    QTcpServer server;
    std::vector<QTcpSocket *> clients;
    QByteArray received;
    int accepted = 0;
    bool dropEveryone = false; ///< Принимать и тут же закрывать: агент без UART именно так себя ведёт.
};

/// \brief Настройки канала для сервера теста.
QVariantMap settingsFor(const Server &server, bool autoReconnect = true)
{
    return {{QStringLiteral("host"), QStringLiteral("127.0.0.1")},
            {QStringLiteral("port"), int(server.port())},
            {QStringLiteral("autoReconnect"), autoReconnect},
            {QStringLiteral("connectTimeoutMs"), 15000}}; // с запасом: под параллельным ctest процесс бывает не получает процессор секундами
}

/// \brief Канал и список состояний, через которые он прошёл.
struct ChannelFixture
{
    ChannelFixture()
    {
        QObject::connect(&channel, &IInterfaceChannel::stateChanged, &channel,
                         [this](ChannelState state, const QString &detail) {
                             states.push_back(state);
                             lastDetail = detail;
                         });
        QObject::connect(&channel, &IInterfaceChannel::dataReceived, &channel,
                         [this](const QByteArray &data, qint64 timestamp) {
                             received.append(data);
                             timestamps.push_back(timestamp);
                         });
    }

    TcpTunnelChannel channel;
    std::vector<ChannelState> states;
    QString lastDetail;
    QByteArray received;
    std::vector<qint64> timestamps;
};

} // namespace

// --- Канал ------------------------------------------------------------------------------

TEST(TcpTunnelChannel, OpensAndRelaysBothDirections)
{
    Server server;
    ChannelFixture f;
    QString error;
    ASSERT_TRUE(f.channel.open(settingsFor(server), &error)) << qPrintable(error);
    EXPECT_EQ(f.channel.state(), ChannelState::Open);
    ASSERT_TRUE(waitSlow([&] { return !server.clients.empty(); }));

    server.clients.front()->write("ping\r\n");
    ASSERT_TRUE(waitSlow([&] { return f.received.size() >= 6; }));
    EXPECT_EQ(f.received, QByteArray("ping\r\n"));

    EXPECT_EQ(f.channel.write("hello"), 5);
    ASSERT_TRUE(waitSlow([&] { return server.received.size() >= 5; }));
    EXPECT_EQ(server.received, QByteArray("hello"));
}

TEST(TcpTunnelChannel, TimestampsComeFromTheMonotonicClockAndNeverGoBack)
{
    Server server;
    ChannelFixture f;
    ASSERT_TRUE(f.channel.open(settingsFor(server), nullptr));
    ASSERT_TRUE(waitSlow([&] { return !server.clients.empty(); }));

    for (int i = 0; i < 3; ++i) {
        server.clients.front()->write("x");
        server.clients.front()->flush();
        ASSERT_TRUE(waitSlow([&, i] { return f.timestamps.size() > size_t(i); }));
    }

    EXPECT_GE(f.timestamps.front(), 0);
    for (size_t i = 1; i < f.timestamps.size(); ++i)
        EXPECT_GE(f.timestamps[i], f.timestamps[i - 1]);
}

TEST(TcpTunnelChannel, RefusedConnectionSaysWhereAndWhy)
{
    const quint16 closedPort = kClosedPort;
    ChannelFixture f;
    QString error;

    const bool opened = f.channel.open({{QStringLiteral("host"), QStringLiteral("127.0.0.1")},
                                        {QStringLiteral("port"), int(closedPort)},
                                        {QStringLiteral("connectTimeoutMs"), 1000}},
                                       &error);

    EXPECT_FALSE(opened);
    EXPECT_EQ(f.channel.state(), ChannelState::Error);
    EXPECT_TRUE(error.contains(QStringLiteral("127.0.0.1:%1").arg(closedPort))) << qPrintable(error);
    EXPECT_FALSE(f.lastDetail.isEmpty());
}

TEST(TcpTunnelChannel, MissingHostIsAClearErrorNotAResolverFailure)
{
    ChannelFixture f;
    QString error;
    EXPECT_FALSE(f.channel.open({{QStringLiteral("host"), QStringLiteral("  ")}}, &error));
    EXPECT_EQ(error, QStringLiteral("The host is not set."));
    EXPECT_EQ(f.channel.state(), ChannelState::Error);
}

TEST(TcpTunnelChannel, DropWithoutAutoReconnectEndsInErrorWithAnExplanation)
{
    Server server;
    ChannelFixture f;
    QStringList reported;
    QObject::connect(&f.channel, &IInterfaceChannel::errorOccurred, &f.channel,
                     [&reported](const QString &message) { reported.append(message); });
    ASSERT_TRUE(f.channel.open(settingsFor(server, /*autoReconnect=*/false), nullptr));
    ASSERT_TRUE(waitSlow([&] { return !server.clients.empty(); }));

    server.clients.front()->disconnectFromHost();

    ASSERT_TRUE(waitSlow([&] { return f.channel.state() == ChannelState::Error; }));
    // Подсказка про последовательный порт — потому что именно так агент без UART выглядит
    // снаружи: принял и закрыл.
    EXPECT_TRUE(f.lastDetail.contains(QStringLiteral("serial port"))) << qPrintable(f.lastDetail);
    EXPECT_EQ(reported.size(), 1);
}

TEST(TcpTunnelChannel, DropWithAutoReconnectGoesUnavailableAndComesBack)
{
    Server server;
    ChannelFixture f;
    ASSERT_TRUE(f.channel.open(settingsFor(server), nullptr));
    ASSERT_TRUE(waitSlow([&] { return !server.clients.empty(); }));

    QTcpSocket *first = server.clients.front();
    first->disconnectFromHost();
    ASSERT_TRUE(waitSlow([&] { return f.channel.state() == ChannelState::Unavailable; }));
    EXPECT_EQ(f.channel.write("lost"), -1); // писать некуда, и молча принять нельзя

    // Попытка через kRetryMs, возврат в Open — после выдержки kSettleMs.
    ASSERT_TRUE(waitSlow([&] { return f.channel.state() == ChannelState::Open; }, 6000));
    EXPECT_EQ(server.accepted, 2);

    ASSERT_TRUE(waitSlow([&] { return server.clients.size() == 2; }));
    server.clients.back()->write("again");
    ASSERT_TRUE(waitSlow([&] { return f.received.contains("again"); }));
}

TEST(TcpTunnelChannel, AgentThatDropsEveryClientDoesNotMakeTheIndicatorFlap)
{
    Server server;
    ChannelFixture f;
    ASSERT_TRUE(f.channel.open(settingsFor(server), nullptr));
    ASSERT_TRUE(waitSlow([&] { return !server.clients.empty(); }));

    // Дальше сервер ведёт себя как агент без UART: принимает и сразу закрывает.
    server.dropEveryone = true;
    server.clients.front()->disconnectFromHost();
    ASSERT_TRUE(waitSlow([&] { return f.channel.state() == ChannelState::Unavailable; }));
    const size_t statesBefore = f.states.size();

    // Дождаться хотя бы одной попытки и убедиться, что «открыто» не промелькнуло.
    ASSERT_TRUE(waitSlow([&] { return server.accepted >= 2; }, 5000));
    waitSlow([] { return false; }, 700); // выдержка и обрыв внутри неё
    EXPECT_EQ(f.channel.state(), ChannelState::Unavailable);
    for (size_t i = statesBefore; i < f.states.size(); ++i)
        EXPECT_NE(f.states[i], ChannelState::Open) << "state #" << i;
}

TEST(TcpTunnelChannel, CloseIsFinalAndTellsTheServer)
{
    Server server;
    ChannelFixture f;
    ASSERT_TRUE(f.channel.open(settingsFor(server), nullptr));
    ASSERT_TRUE(waitSlow([&] { return !server.clients.empty(); }));

    f.channel.close();

    EXPECT_EQ(f.channel.state(), ChannelState::Closed);
    EXPECT_TRUE(waitSlow(
        [&] { return server.clients.front()->state() == QAbstractSocket::UnconnectedState; }));
    EXPECT_EQ(f.channel.write("x"), -1);
    f.channel.close(); // повторный вызов безопасен
    EXPECT_EQ(f.channel.state(), ChannelState::Closed);
}

TEST(TcpTunnelChannel, CloseDuringReconnectStopsRetrying)
{
    Server server;
    ChannelFixture f;
    ASSERT_TRUE(f.channel.open(settingsFor(server), nullptr));
    ASSERT_TRUE(waitSlow([&] { return !server.clients.empty(); }));
    server.clients.front()->disconnectFromHost();
    ASSERT_TRUE(waitSlow([&] { return f.channel.state() == ChannelState::Unavailable; }));

    f.channel.close();
    // Что успело дойти до закрытия, не в счёт: на загруженной машине между этими двумя строками
    // проходит и больше kRetryMs. Считается только то, что пришло ПОСЛЕ закрытия.
    waitFor([] { return false; }, 150);
    const int acceptedAtClose = server.accepted;
    waitFor([] { return false; }, 2600); // дольше kRetryMs

    EXPECT_EQ(server.accepted, acceptedAtClose);
    EXPECT_EQ(f.channel.state(), ChannelState::Closed);
}

// --- Адрес подключения ------------------------------------------------------------------

TEST(TcpTunnelEndpoint, ComposesHostAndPort)
{
    EXPECT_EQ(tcptunnel::endpointText({{QStringLiteral("host"), QStringLiteral("pi-lab")},
                                       {QStringLiteral("port"), 7778}}),
              QStringLiteral("pi-lab:7778"));
}

TEST(TcpTunnelEndpoint, DefaultsToThePortOfTheFirstAgent)
{
    EXPECT_EQ(tcptunnel::endpointText({{QStringLiteral("host"), QStringLiteral("10.0.0.5")}}),
              QStringLiteral("10.0.0.5:7777"));
}

TEST(TcpTunnelEndpoint, Ipv6AddressIsBracketedSoThePortStaysReadable)
{
    EXPECT_EQ(tcptunnel::endpointText({{QStringLiteral("host"), QStringLiteral("fe80::1")},
                                       {QStringLiteral("port"), 7777}}),
              QStringLiteral("[fe80::1]:7777"));
}

TEST(TcpTunnelEndpoint, EmptyHostGivesNoEndpoint)
{
    EXPECT_TRUE(tcptunnel::endpointText({{QStringLiteral("host"), QStringLiteral("  ")}}).isEmpty());
}

// --- Плагин -----------------------------------------------------------------------------

TEST(TcpTunnelPlugin, EnumeratesNothingAndLetsTheUserCreateConnections)
{
    TcpTunnelPlugin plugin;
    EXPECT_TRUE(plugin.supportsUserDevices());
    EXPECT_TRUE(plugin.enumerate().isEmpty());
    EXPECT_EQ(plugin.pluginId(), QStringLiteral("tcptunnel"));
}

TEST(TcpTunnelPlugin, HostIsRequiredAndPortDefaultsToTheFirstAgent)
{
    TcpTunnelPlugin plugin;
    const SettingsSchema schema = plugin.settingsSchema();

    ASSERT_NE(schema.field(QStringLiteral("host")), nullptr);
    EXPECT_TRUE(schema.field(QStringLiteral("host"))->required);
    EXPECT_EQ(schema.defaults().value(QStringLiteral("port")).toInt(), 7777);
    EXPECT_EQ(schema.missingRequiredFields(schema.defaults()), QStringList{QStringLiteral("host")});
}

TEST(TcpTunnelPlugin, RegistryNamesAConnectionByItsAddress)
{
    TempDir dir;
    PluginManager plugins;
    TcpTunnelPlugin plugin;
    SettingsStore store(dir.filePath(QStringLiteral("interfaces.json")));
    InterfaceRegistry registry(&plugins, &store);
    ASSERT_TRUE(plugins.addPlugin(&plugin));
    store.load();

    const QString id = registry.addUserDevice(QStringLiteral("tcptunnel"));
    ASSERT_FALSE(id.isEmpty());
    EXPECT_EQ(registry.entry(id)->descriptor.systemName, QStringLiteral("New connection"));

    registry.setSettingsFor(id, {{QStringLiteral("host"), QStringLiteral("pi-lab")},
                                 {QStringLiteral("port"), 7778}});

    // Адрес стал и подписью: пока пользователь не задал псевдоним, именно он на виду.
    EXPECT_EQ(registry.entry(id)->descriptor.systemName, QStringLiteral("pi-lab:7778"));
    EXPECT_EQ(registry.entry(id)->displayName(), QStringLiteral("pi-lab:7778"));
}

TEST(TcpTunnelPlugin, CreatesAChannelForAnyDescriptor)
{
    TcpTunnelPlugin plugin;
    std::unique_ptr<IInterfaceChannel> channel(plugin.createChannel(InterfaceDescriptor{}));
    ASSERT_NE(channel, nullptr);
    EXPECT_EQ(channel->state(), ChannelState::Closed);
}

// --- Диапазоны поиска -------------------------------------------------------------------

namespace {

QStringList addressStrings(const QList<QHostAddress> &addresses)
{
    QStringList result;
    for (const QHostAddress &address : addresses)
        result << address.toString();
    return result;
}

} // namespace

TEST(NetworkScannerRange, SingleAddress)
{
    QList<QHostAddress> targets;
    ASSERT_TRUE(NetworkScanner::parseRange(QStringLiteral("10.0.0.5"), &targets, nullptr));
    EXPECT_EQ(addressStrings(targets), QStringList{QStringLiteral("10.0.0.5")});
}

TEST(NetworkScannerRange, NetworkExcludesTheNetworkAndBroadcastAddresses)
{
    QList<QHostAddress> targets;
    ASSERT_TRUE(NetworkScanner::parseRange(QStringLiteral("192.168.1.0/24"), &targets, nullptr));
    ASSERT_EQ(targets.size(), 254);
    EXPECT_EQ(targets.first().toString(), QStringLiteral("192.168.1.1"));
    EXPECT_EQ(targets.last().toString(), QStringLiteral("192.168.1.254"));
}

TEST(NetworkScannerRange, AnAddressInsideANetworkMeansTheWholeNetwork)
{
    // Так пишут, копируя адрес интерфейса с маской: 192.168.1.37/24.
    QList<QHostAddress> targets;
    ASSERT_TRUE(NetworkScanner::parseRange(QStringLiteral("192.168.1.37/24"), &targets, nullptr));
    EXPECT_EQ(targets.size(), 254);
}

TEST(NetworkScannerRange, SmallNetworks)
{
    QList<QHostAddress> targets;
    ASSERT_TRUE(NetworkScanner::parseRange(QStringLiteral("10.0.0.0/30"), &targets, nullptr));
    EXPECT_EQ(addressStrings(targets), (QStringList{QStringLiteral("10.0.0.1"), QStringLiteral("10.0.0.2")}));

    // /31 и /32 — точка–точка: адресов «сети» и «широковещательного» там нет.
    ASSERT_TRUE(NetworkScanner::parseRange(QStringLiteral("10.0.0.0/31"), &targets, nullptr));
    EXPECT_EQ(targets.size(), 2);
    ASSERT_TRUE(NetworkScanner::parseRange(QStringLiteral("10.0.0.7/32"), &targets, nullptr));
    EXPECT_EQ(targets.size(), 1);
}

TEST(NetworkScannerRange, SeparatorsAndDuplicates)
{
    QList<QHostAddress> targets;
    ASSERT_TRUE(NetworkScanner::parseRange(
        QStringLiteral("10.0.0.1, 10.0.0.2;10.0.0.1\n10.0.0.3"), &targets, nullptr));
    EXPECT_EQ(addressStrings(targets),
              (QStringList{QStringLiteral("10.0.0.1"), QStringLiteral("10.0.0.2"),
                           QStringLiteral("10.0.0.3")}));
}

TEST(NetworkScannerRange, EmptyTextIsAnEmptyRangeNotAnError)
{
    QList<QHostAddress> targets;
    EXPECT_TRUE(NetworkScanner::parseRange(QString(), &targets, nullptr));
    EXPECT_TRUE(targets.isEmpty());
}

TEST(NetworkScannerRange, NamesTheTokenItCouldNotRead)
{
    QList<QHostAddress> targets;
    QString error;
    EXPECT_FALSE(NetworkScanner::parseRange(QStringLiteral("10.0.0.1, pi-lab"), &targets, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("pi-lab"))) << qPrintable(error);
    EXPECT_TRUE(targets.isEmpty());

    EXPECT_FALSE(NetworkScanner::parseRange(QStringLiteral("fe80::1"), &targets, &error)); // только IPv4
    EXPECT_FALSE(NetworkScanner::parseRange(QStringLiteral("10.0.0.0/33"), &targets, &error));
}

TEST(NetworkScannerRange, TooLargeIsRefusedBeforeTheAddressesAreGenerated)
{
    QList<QHostAddress> targets;
    QString error;
    EXPECT_FALSE(NetworkScanner::parseRange(QStringLiteral("10.0.0.0/8"), &targets, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("too large"))) << qPrintable(error);

    // /20 — как раз потолок, а два таких вместе его превышают.
    EXPECT_TRUE(NetworkScanner::parseRange(QStringLiteral("192.168.0.0/20"), &targets, nullptr));
    EXPECT_EQ(targets.size(), 4094);
    EXPECT_FALSE(NetworkScanner::parseRange(QStringLiteral("192.168.0.0/20, 10.0.0.0/20"), &targets, &error));
}

TEST(NetworkScannerRange, LocalNetworksAreOfferedAsUniqueCidrsWithTheirInterface)
{
    const auto networks = NetworkScanner::localNetworks();
    QSet<QString> seen;
    for (const NetworkScanner::LocalNetwork &network : networks) {
        EXPECT_FALSE(seen.contains(network.cidr)) << qPrintable(network.cidr);
        seen.insert(network.cidr);
        EXPECT_FALSE(network.interfaceName.isEmpty());
        // Запись понимает сам сканер, и она описывает именно сеть, а не адрес хоста внутри неё.
        const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(network.cidr);
        ASSERT_FALSE(subnet.first.isNull()) << qPrintable(network.cidr);
        EXPECT_GE(subnet.second, 22);
        EXPECT_LE(subnet.second, 30);
        EXPECT_EQ(subnet.first.toIPv4Address() & (0xFFFFFFFFu << (32 - subnet.second)), subnet.first.toIPv4Address())
            << qPrintable(network.cidr);
    }
}

TEST(NetworkScannerRange, LocalNetworksAreIpv4AndUnique)
{
    // Что именно вернётся, зависит от машины (в контейнере сетей может не быть вовсе).
    const QList<QHostAddress> targets = NetworkScanner::localTargets();
    QSet<quint32> seen;
    for (const QHostAddress &address : targets) {
        EXPECT_EQ(address.protocol(), QAbstractSocket::IPv4Protocol);
        EXPECT_FALSE(seen.contains(address.toIPv4Address())) << qPrintable(address.toString());
        seen.insert(address.toIPv4Address());
        EXPECT_FALSE(address.isLoopback());
    }
    EXPECT_LE(targets.size(), 4 * 1022 * 4); // сети шире /22 сужены до /24
}

// --- Сканер ------------------------------------------------------------------------------

namespace {

NetworkScanner::Options localOptions(quint16 firstPort)
{
    NetworkScanner::Options options;
    options.targets = {QHostAddress::LocalHost};
    options.firstPort = firstPort;
    options.maxPorts = 8;
    // С запасом: на петле соединение мгновенное, но под параллельным ctest процесс бывает не
    // получает процессор дольше боевых 500 мс, и сработавший таймер выдал бы открытый порт за закрытый.
    options.timeoutMs = 5000;
    // Контрольный порт — тот, где заведомо никто не слушает: свободный эфемерный под параллельным
    // ctest мог бы оказаться чужим сервером, и проверка принялa бы хост за «открытый на всём».
    options.controlPorts = {kClosedPort};
    options.resolveNames = false; // имя loopback зависит от машины и ничего не проверяет
    return options;
}

/**
 * \brief Подряд идущие свободные порты на петле: серверы слушают их все.
 *
 * Порт сразу за блоком обязан быть закрыт, иначе проверка «сканер остановился на первом
 * закрытом» зависела бы от соседей: ctest гоняет тесты параллельными процессами, а порты
 * система раздаёт подряд, так что рядом очень часто оказывается чужой сервер.
 */
bool listenConsecutive(std::vector<std::unique_ptr<QTcpServer>> &servers, int count)
{
    QString lastError;
    // Первые попытки — порты, которые выдаёт система, дальше берём подряд идущие сами: на Windows
    // соседний с выданным занять не удавалось ни разу.
    const quint16 fixedStart = quint16(20000 + QRandomGenerator::global()->bounded(20000));
    for (int attempt = 0; attempt < 100; ++attempt) {
        servers.clear();
        quint16 base = attempt < 50 ? quint16(0) : quint16(fixedStart + (attempt - 50) * (count + 2));
        bool all = true;
        for (int i = 0; i < count && all; ++i) {
            auto next = std::make_unique<QTcpServer>();
            all = next->listen(QHostAddress::LocalHost, i == 0 ? base : quint16(base + i));
            if (!all)
                lastError = next->errorString();
            else if (i == 0)
                base = next->serverPort();
            if (all)
                servers.push_back(std::move(next));
        }
        if (!all)
            continue;

        // Порты за блоком: тот, что должен остановить сканер, и следующий за ним.
        QTcpSocket probe;
        bool foreignNeighbour = false;
        for (int beyond = count; beyond <= count + 1 && !foreignNeighbour; ++beyond) {
            probe.connectToHost(QHostAddress::LocalHost, quint16(base + beyond));
            foreignNeighbour = probe.waitForConnected(200);
            probe.abort();
        }
        if (!foreignNeighbour)
            return true;
        lastError = QStringLiteral("a foreign server next to the block");
    }
    ADD_FAILURE() << "no " << count << " consecutive free ports: " << qPrintable(lastError);
    return false;
}

/// \brief Дождаться конца прохода. Долго: Windows сообщает об отказе на петле лишь через 1–2 с (повторы SYN),
///        и каждый закрытый порт стоит столько.
bool waitForScan(const NetworkScanner &scanner)
{
    return waitFor([&] { return !scanner.isRunning(); }, 30000);
}

QList<int> portsOf(const NetworkScanner &scanner, const QString &address)
{
    for (const NetworkScanner::Host &host : scanner.results()) {
        if (host.address.toString() == address)
            return host.ports;
    }
    return {};
}

} // namespace

TEST(NetworkScanner, FindsAnOpenPort)
{
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    NetworkScanner scanner;
    int finished = 0;
    QObject::connect(&scanner, &NetworkScanner::finished, &scanner, [&finished] { ++finished; });

    // Один порт на хост: рядом может оказаться сервер параллельно идущего теста, а здесь
    // проверяется лишь то, что открытый порт находится.
    NetworkScanner::Options options = localOptions(server.serverPort());
    options.maxPorts = 1;
    scanner.start(options);
    EXPECT_TRUE(scanner.isRunning());
    ASSERT_TRUE(waitForScan(scanner));

    EXPECT_EQ(portsOf(scanner, QStringLiteral("127.0.0.1")), QList<int>{server.serverPort()});
    EXPECT_EQ(scanner.hostsDone(), 1);
    EXPECT_EQ(scanner.targetCount(), 1);
    EXPECT_EQ(finished, 1);
}

TEST(NetworkScanner, ClosedPortFindsNothingAndStillFinishes)
{
    NetworkScanner scanner;
    scanner.start(localOptions(kClosedPort));
    ASSERT_TRUE(waitForScan(scanner));

    EXPECT_TRUE(scanner.results().isEmpty());
    EXPECT_EQ(scanner.hostsDone(), 1);
}

TEST(NetworkScanner, FollowsConsecutivePortsUpToTheFirstClosedOne)
{
    std::vector<std::unique_ptr<QTcpServer>> servers;
    ASSERT_TRUE(listenConsecutive(servers, 3));
    const quint16 base = servers.front()->serverPort();

    NetworkScanner scanner;
    scanner.start(localOptions(base));
    ASSERT_TRUE(waitForScan(scanner));

    EXPECT_EQ(portsOf(scanner, QStringLiteral("127.0.0.1")),
              (QList<int>{base, base + 1, base + 2}));
}

TEST(NetworkScanner, StopsAtAGapEvenIfALaterPortIsOpen)
{
    // Агенты занимают порты подряд, и дыра значит, что дальше их нет. Искать за дырой — это
    // проверять шестнадцать портов на каждом хосте ради случая, которого не бывает.
    std::vector<std::unique_ptr<QTcpServer>> servers;
    ASSERT_TRUE(listenConsecutive(servers, 4));
    const quint16 base = servers.front()->serverPort();
    servers[2]->close(); // base + 2 закрыт, base + 3 открыт

    NetworkScanner scanner;
    scanner.start(localOptions(base));
    ASSERT_TRUE(waitForScan(scanner));

    EXPECT_EQ(portsOf(scanner, QStringLiteral("127.0.0.1")), (QList<int>{base, base + 1}));
}

TEST(NetworkScanner, HostWhoseFirstPortIsClosedIsNotProbedFurther)
{
    std::vector<std::unique_ptr<QTcpServer>> servers;
    ASSERT_TRUE(listenConsecutive(servers, 2));
    const quint16 base = servers.front()->serverPort();
    servers[0]->close(); // первый закрыт, второй открыт

    NetworkScanner scanner;
    scanner.start(localOptions(base));
    ASSERT_TRUE(waitForScan(scanner));

    EXPECT_TRUE(scanner.results().isEmpty());
}

TEST(NetworkScanner, ByDefaultTheFirstPortAndThreeFollowingAreChecked)
{
    EXPECT_EQ(NetworkScanner::Options{}.maxPorts, 4); // 7777 и три следующих

    std::vector<std::unique_ptr<QTcpServer>> servers;
    ASSERT_TRUE(listenConsecutive(servers, 5));
    const quint16 base = servers.front()->serverPort();

    NetworkScanner scanner;
    NetworkScanner::Options options = localOptions(base);
    options.maxPorts = NetworkScanner::Options{}.maxPorts; // умолчание, а не тестовые восемь
    scanner.start(options);
    ASSERT_TRUE(waitForScan(scanner));

    // Пятый порт открыт, но за пределом: дальше трёх следующих агентов на одной машине не бывает.
    EXPECT_EQ(portsOf(scanner, QStringLiteral("127.0.0.1")), (QList<int>{base, base + 1, base + 2, base + 3}));
}

TEST(NetworkScanner, HostThatAcceptsEveryPortIsDroppedAsIntercepted)
{
    // Первый порт открыт, но и «ничей» контрольный порт тоже принимает соединения: это не агент, а
    // роутер, VPN или прозрачный прокси, который отвечает за хост на любой порт.
    QTcpServer agent;
    QTcpServer catchAll;
    ASSERT_TRUE(agent.listen(QHostAddress::LocalHost, 0));
    ASSERT_TRUE(catchAll.listen(QHostAddress::LocalHost, 0));

    NetworkScanner scanner;
    NetworkScanner::Options options = localOptions(agent.serverPort());
    // Один порт на хост: порты, выданные подряд, иначе контрольный попал бы в диапазон поиска, а
    // контрольный порт внутри поиска сканер, по замыслу, не использует.
    options.maxPorts = 1;
    options.controlPorts = {catchAll.serverPort()};
    scanner.start(options);
    ASSERT_TRUE(waitForScan(scanner));

    EXPECT_TRUE(scanner.results().isEmpty());
    EXPECT_EQ(scanner.interceptedCount(), 1);
    EXPECT_EQ(scanner.hostsDone(), 1);
}

TEST(NetworkScanner, InterceptedHostGetsNoFollowUpProbes)
{
    std::vector<std::unique_ptr<QTcpServer>> servers;
    ASSERT_TRUE(listenConsecutive(servers, 2));
    const quint16 base = servers.front()->serverPort();
    QTcpServer catchAll;
    ASSERT_TRUE(catchAll.listen(QHostAddress::LocalHost, 0));
    int accepted = 0;
    QObject::connect(servers[1].get(), &QTcpServer::newConnection, servers[1].get(), [&] { ++accepted; });

    NetworkScanner scanner;
    NetworkScanner::Options options = localOptions(base);
    options.maxPorts = 2; // base и base + 1; контрольный порт — за пределами
    options.controlPorts = {catchAll.serverPort()};
    scanner.start(options);
    ASSERT_TRUE(waitForScan(scanner));
    waitFor([] { return false; }, 100);

    // Второй порт хоста, признанного перехватом, никто не опрашивал: тратить на него время незачем.
    EXPECT_EQ(accepted, 0);
}

TEST(NetworkScanner, VerificationCanBeSwitchedOffAndControlPortsInsideTheSearchAreIgnored)
{
    QTcpServer agent;
    QTcpServer other;
    ASSERT_TRUE(agent.listen(QHostAddress::LocalHost, 0));
    ASSERT_TRUE(other.listen(QHostAddress::LocalHost, 0));

    {
        NetworkScanner scanner;
        NetworkScanner::Options options = localOptions(agent.serverPort());
        options.controlPorts = {other.serverPort()};
        options.verifyHosts = false;
        scanner.start(options);
        ASSERT_TRUE(waitForScan(scanner));
        EXPECT_EQ(scanner.results().size(), 1);
        EXPECT_EQ(scanner.interceptedCount(), 0);
    }
    {
        // Контрольный порт, совпавший с искомым, принял бы настоящий агент за перехват.
        NetworkScanner scanner;
        NetworkScanner::Options options = localOptions(agent.serverPort());
        options.maxPorts = 1;
        options.controlPorts = {agent.serverPort()};
        scanner.start(options);
        ASSERT_TRUE(waitForScan(scanner));
        EXPECT_EQ(scanner.results().size(), 1);
        EXPECT_EQ(scanner.interceptedCount(), 0);
    }
}

TEST(NetworkScanner, NumberOfPortsPerHostIsLimited)
{
    std::vector<std::unique_ptr<QTcpServer>> servers;
    ASSERT_TRUE(listenConsecutive(servers, 4));
    const quint16 base = servers.front()->serverPort();

    NetworkScanner scanner;
    NetworkScanner::Options options = localOptions(base);
    options.maxPorts = 2;
    scanner.start(options);
    ASSERT_TRUE(waitForScan(scanner));

    EXPECT_EQ(portsOf(scanner, QStringLiteral("127.0.0.1")), (QList<int>{base, base + 1}));
}

TEST(NetworkScanner, NewScanForgetsTheOldResults)
{
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    NetworkScanner scanner;
    scanner.start(localOptions(server.serverPort()));
    ASSERT_TRUE(waitForScan(scanner));
    ASSERT_EQ(scanner.results().size(), 1);

    server.close();
    scanner.start(localOptions(server.serverPort() ? server.serverPort() : 1));
    // Результаты сброшены уже в момент запуска, а не после прохода.
    EXPECT_TRUE(scanner.results().isEmpty());
    ASSERT_TRUE(waitForScan(scanner));
}

TEST(NetworkScanner, ManyTargetsAreProbedInParallelButNeverAboveTheLimit)
{
    const quint16 port = kClosedPort;
    // Адреса 127.0.0.0/26: ни к настоящей сети, ни к VPN это отношения не имеет (на машине с
    // перехватом чужие адреса «открыты» все подряд). На Linux они отказывают сразу, на macOS
    // молчат до таймаута — поэтому таймаут короткий, а адресов немного.
    NetworkScanner scanner;
    NetworkScanner::Options options;
    ASSERT_TRUE(NetworkScanner::parseRange(QStringLiteral("127.0.0.0/26"), &options.targets, nullptr));
    options.firstPort = port;
    options.concurrency = 16;
    options.timeoutMs = 300;
    options.resolveNames = false;

    scanner.start(options);
    // Первые соединения запущены синхронно из start(), до того как хоть один ответ обработан: если
    // предел не соблюдается, здесь видно сразу все 62.
    EXPECT_EQ(scanner.activeProbes(), 16);

    ASSERT_TRUE(waitFor([&] { return !scanner.isRunning(); }, 30000));
    EXPECT_EQ(scanner.hostsDone(), 62);
    EXPECT_EQ(scanner.activeProbes(), 0);
}

TEST(NetworkScanner, WarnsWhenAMiddlemanAnswersForMostAddresses)
{
    EXPECT_FALSE(NetworkScanner::looksIntercepted(0, 254));
    EXPECT_FALSE(NetworkScanner::looksIntercepted(3, 254));   // мало: бывает, что в сети несколько агентов
    EXPECT_FALSE(NetworkScanner::looksIntercepted(7, 10));    // всё ответило, но хостов слишком мало для вывода
    EXPECT_FALSE(NetworkScanner::looksIntercepted(100, 254)); // меньше половины
    EXPECT_TRUE(NetworkScanner::looksIntercepted(254, 254));
    EXPECT_TRUE(NetworkScanner::looksIntercepted(130, 254));
}

TEST(NetworkScanner, CancelStopsEverythingAndKeepsWhatWasFound)
{
    NetworkScanner scanner;
    NetworkScanner::Options options;
    ASSERT_TRUE(NetworkScanner::parseRange(QStringLiteral("127.0.0.0/26"), &options.targets, nullptr));
    options.timeoutMs = 300;
    options.resolveNames = false;
    int finished = 0;
    QObject::connect(&scanner, &NetworkScanner::finished, &scanner, [&finished] { ++finished; });

    scanner.start(options);
    ASSERT_TRUE(scanner.isRunning());
    scanner.cancel();

    EXPECT_FALSE(scanner.isRunning());
    EXPECT_EQ(finished, 1);
    scanner.cancel(); // повторная отмена безопасна и ничего не сообщает
    EXPECT_EQ(finished, 1);

    // Запоздавшие соединения прерванного прохода ничего не меняют.
    waitFor([] { return false; }, 300);
    EXPECT_FALSE(scanner.isRunning());
}

TEST(NetworkScanner, EmptyTargetListFinishesImmediately)
{
    NetworkScanner scanner;
    int finished = 0;
    QObject::connect(&scanner, &NetworkScanner::finished, &scanner, [&finished] { ++finished; });
    scanner.start(NetworkScanner::Options{});
    EXPECT_FALSE(scanner.isRunning());
    EXPECT_EQ(finished, 1);
}

// --- Поиск в плагине --------------------------------------------------------------------

TEST(TcpTunnelPlugin, ScanFieldsAreDeclaredAndHoldNoValue)
{
    TcpTunnelPlugin plugin;
    const SettingsSchema schema = plugin.settingsSchema();

    ASSERT_NE(schema.field(QStringLiteral("scan")), nullptr);
    EXPECT_EQ(schema.field(QStringLiteral("scan"))->type, SettingsField::Action);
    ASSERT_NE(schema.field(QStringLiteral("scanStatus")), nullptr);
    EXPECT_TRUE(schema.field(QStringLiteral("scanStatus"))->live);

    // Адрес и порт — списки с живыми пунктами, но набрать можно и своё.
    for (const char *key : {"host", "port"}) {
        const SettingsField *field = schema.field(QString::fromLatin1(key));
        ASSERT_NE(field, nullptr) << key;
        EXPECT_EQ(field->type, SettingsField::Choice) << key;
        EXPECT_TRUE(field->live) << key;
        EXPECT_TRUE(field->editable) << key;
    }

    EXPECT_FALSE(schema.defaults().contains(QStringLiteral("scan")));
    EXPECT_FALSE(schema.defaults().contains(QStringLiteral("scanStatus")));
    EXPECT_EQ(schema.defaults().value(QStringLiteral("scanPort")).toInt(), 7777);
}

TEST(TcpTunnelPlugin, ScanOfABadRangeReturnsTheReasonAndStartsNothing)
{
    TcpTunnelPlugin plugin;
    const QString problem = plugin.triggerAction(
        InterfaceDescriptor{}, QStringLiteral("scan"), {{QStringLiteral("scanRange"), QStringLiteral("pi-lab")}});
    EXPECT_TRUE(problem.contains(QStringLiteral("pi-lab"))) << qPrintable(problem);
    EXPECT_TRUE(plugin.liveOptions({}, QStringLiteral("scanStatus"), {}).isEmpty());
}

TEST(TcpTunnelPlugin, UnknownActionIsIgnored)
{
    TcpTunnelPlugin plugin;
    EXPECT_TRUE(plugin.triggerAction(InterfaceDescriptor{}, QStringLiteral("nonsense"), {}).isEmpty());
}

TEST(TcpTunnelPlugin, FoundHostsAndPortsAreOfferedThroughTheLiveFields)
{
    std::vector<std::unique_ptr<QTcpServer>> servers;
    ASSERT_TRUE(listenConsecutive(servers, 2));
    const int base = servers.front()->serverPort();

    TcpTunnelPlugin plugin;
    const InterfaceDescriptor none;

    // До первого поиска предлагать нечего, а строка результата молчит: остаётся её текст по умолчанию.
    EXPECT_TRUE(plugin.liveOptions(none, QStringLiteral("host"), {}).isEmpty());
    EXPECT_TRUE(plugin.liveOptions(none, QStringLiteral("scanStatus"), {}).isEmpty());

    const QVariantMap settings{{QStringLiteral("scanRange"), QStringLiteral("127.0.0.1")},
                               {QStringLiteral("scanPort"), base}};
    ASSERT_TRUE(plugin.triggerAction(none, QStringLiteral("scan"), settings).isEmpty());
    ASSERT_TRUE(waitFor([&] {
        const auto status = plugin.liveOptions(none, QStringLiteral("scanStatus"), {});
        return !status.isEmpty() && status.first().label.contains(QStringLiteral("finished"));
    }, 30000)); // три раунда по таймауту пробы: на Windows закрытые порты петли отвечают не сразу

    // Найденное — успех: значок «сделано», а не нейтральная строка.
    EXPECT_EQ(plugin.liveOptions(none, QStringLiteral("scanStatus"), {}).first().value.toInt(),
              int(NoteSeverity::Ok));

    const auto hosts = plugin.liveOptions(none, QStringLiteral("host"), {});
    ASSERT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts.first().value.toString(), QStringLiteral("127.0.0.1"));
    EXPECT_TRUE(hosts.first().label.contains(QString::number(base)));
    EXPECT_TRUE(hosts.first().label.contains(QString::number(base + 1)));

    // Порты предлагаются для выбранного хоста, и только для него.
    const auto ports = plugin.liveOptions(none, QStringLiteral("port"),
                                          {{QStringLiteral("host"), QStringLiteral("127.0.0.1")}});
    // Не меньше двух: поиск плагина идёт с настоящими параметрами (три следующих порта), и под
    // параллельным ctest сосед за блоком может оказаться чужим сервером; важны первые два и порядок.
    ASSERT_GE(ports.size(), 2);
    EXPECT_EQ(ports.at(0).value.toInt(), base);
    EXPECT_EQ(ports.at(1).value.toInt(), base + 1);
    EXPECT_TRUE(plugin.liveOptions(none, QStringLiteral("port"),
                                   {{QStringLiteral("host"), QStringLiteral("10.9.9.9")}}).isEmpty());
    EXPECT_TRUE(plugin.liveOptions(none, QStringLiteral("port"), {}).isEmpty());
}

TEST(TcpTunnelPlugin, ScanThatFindsNothingSaysWhereItLooked)
{
    const quint16 port = kClosedPort;
    TcpTunnelPlugin plugin;
    ASSERT_TRUE(plugin.triggerAction(InterfaceDescriptor{}, QStringLiteral("scan"),
                                     {{QStringLiteral("scanRange"), QStringLiteral("127.0.0.1")},
                                      {QStringLiteral("scanPort"), int(port)}})
                    .isEmpty());
    ASSERT_TRUE(waitFor([&] {
        const auto status = plugin.liveOptions({}, QStringLiteral("scanStatus"), {});
        return !status.isEmpty() && status.first().label.startsWith(QStringLiteral("Nothing found"));
    }, 30000));
    EXPECT_TRUE(plugin.liveOptions({}, QStringLiteral("scanStatus"), {}).first().label.contains(
        QStringLiteral("127.0.0.1")));
    // Пустой итог — предупреждение, а не сбой и не успех: искать надо иначе, но ничего не сломалось.
    EXPECT_EQ(plugin.liveOptions({}, QStringLiteral("scanStatus"), {}).first().value.toInt(),
              int(NoteSeverity::Warning));
}

TEST(TcpTunnelChannel, PortOutsideTheRangeIsRefusedInsteadOfWrappingAround)
{
    // 70000 как quint16 — это 4464, чужой порт.
    ChannelFixture f;
    QString error;
    EXPECT_FALSE(f.channel.open({{QStringLiteral("host"), QStringLiteral("127.0.0.1")},
                                 {QStringLiteral("port"), 70000}},
                                &error));
    EXPECT_EQ(error, QStringLiteral("Invalid port."));
}

// --- Комплект агента в ресурсах ---------------------------------------------------------

TEST(ResourceAgentPackageTest, HoldsEveryBinaryAndEveryInstallFile)
{
    const auto resourcesPtr = tcptunnel::resourceAgentPackage();
    const tcptunnel::AgentPackage &resources = *resourcesPtr;
    tcptunnel::AgentPackage disk(QStringLiteral(SPOTTY_AGENT_BINARIES_DIR), QStringLiteral(SPOTTY_AGENT_DIR));

    EXPECT_EQ(resources.version(), disk.version());
    EXPECT_FALSE(resources.version().isEmpty());

    for (const QString &arch : {QStringLiteral("x86_64"), QStringLiteral("i386"), QStringLiteral("aarch64"),
                                QStringLiteral("armv7"), QStringLiteral("armv6"), QStringLiteral("armel"),
                                QStringLiteral("riscv64")}) {
        const QByteArray binary = resources.binary(arch);
        EXPECT_TRUE(binary.startsWith(QByteArray("\x7f" "ELF", 4))) << qPrintable(arch);
        EXPECT_EQ(binary, disk.binary(arch)) << qPrintable(arch);
    }

    for (const QString &name : {QStringLiteral("detect.sh"), QStringLiteral("install.sh"),
                                QStringLiteral("uninstall.sh"), QStringLiteral("spotty-uartd@.service"),
                                QStringLiteral("uartd.conf.template")}) {
        EXPECT_FALSE(resources.file(name).isEmpty()) << qPrintable(name);
        EXPECT_EQ(resources.file(name), disk.file(name)) << qPrintable(name);
    }
}

TEST(ResourceAgentPackageTest, RefusesNamesThatWouldEscapeTheirDirectory)
{
    const auto resources = tcptunnel::resourceAgentPackage();
    const tcptunnel::AgentPackage disk(QStringLiteral(SPOTTY_AGENT_BINARIES_DIR), QStringLiteral(SPOTTY_AGENT_DIR));
    for (const tcptunnel::AgentPackage *package : std::initializer_list<const tcptunnel::AgentPackage *>{resources.get(), &disk}) {
        EXPECT_TRUE(package->binary(QStringLiteral("../../etc/passwd")).isEmpty());
        EXPECT_TRUE(package->binary(QStringLiteral("x86_64/../aarch64")).isEmpty());
        EXPECT_TRUE(package->binary(QString()).isEmpty());
        EXPECT_TRUE(package->binary(QStringLiteral("no-such-arch")).isEmpty());
        EXPECT_TRUE(package->file(QStringLiteral("../install.sh")).isEmpty());
        EXPECT_TRUE(package->file(QStringLiteral("nope.sh")).isEmpty());
    }
}
