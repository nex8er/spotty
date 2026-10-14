/**
 * \file test_tcptunnel_install.cpp
 * \brief Установка агента через плагин: поля схемы, действия, живые поля, фоновые операции.
 *
 * Плагин работает с подставным сеансом (support/LocalShellSession.h), «удалённая машина» которого —
 * песочница на этом компьютере. Поэтому цепочка «найти порты → выбрать → установить» проходит до
 * запущенного настоящего агента и обмена байтами через псевдотерминал.
 */
#include "AgentPackage.h"
#include "NetworkScanner.h"
#include "TcpTunnelPlugin.h"

#include "support/LocalShellSession.h"
#include "support/UartdSupport.h"

#include <QTcpServer>

#include <atomic>
#include <thread>

using namespace spotty;
using namespace spotty::tcptunnel;
using namespace spotty::test;

namespace {

QString debianDetect(bool systemd = false, int uid = 1000)
{
    return QStringLiteral("uname_s=Linux\nuname_m=x86_64\ndpkg_arch=amd64\nlong_bit=64\nuid=%1\n"
                          "os_name=Debian GNU/Linux 12\nsystemd=%2\nsudo=yes\n")
        .arg(uid)
        .arg(systemd ? QStringLiteral("yes") : QStringLiteral("no"));
}

/// \brief Плагин с подставным ssh и песочницей вместо удалённой машины.
struct PluginFixture
{
    TempDir dir;
    TcpTunnelPlugin plugin;
    std::shared_ptr<NativeAgentPackage> package = std::make_shared<NativeAgentPackage>(
        QStringLiteral(SPOTTY_UARTD_PATH), QStringLiteral(SPOTTY_AGENT_DIR), QStringLiteral(SPOTTY_AGENT_BINARIES_DIR));
    QString sandbox = dir.filePath(QStringLiteral("sandbox"));
    std::function<void(LocalShellSession &)> configure = [](LocalShellSession &) {};
    InterfaceDescriptor device;

    explicit PluginFixture(bool withSsh = true, bool hostKeyKnown = true)
    {
        device.id = QStringLiteral("tcptunnel:user:test");
        if (hostKeyKnown) {
            // Ключ хоста уже известен Spotty: большинству проверок вопрос о доверии не нужен, а сам
            // вопрос проверяется отдельным тестом.
            QDir().mkpath(dataDirectory());
            {
                QFile known(dataDirectory() + QStringLiteral("/known_hosts"));
                if (known.open(QIODevice::WriteOnly))
                    known.write("127.0.0.1:22\tSHA256:testfingerprint\n");
            } // закрыт до того, как плагин прочитает файл
            plugin.setDataDirectory(dataDirectory());
        }
        if (withSsh) {
            plugin.setSshForTesting(
                [this] {
                    auto session = std::make_unique<LocalShellSession>(sandbox);
                    session->cannedDetect = debianDetect();
                    configure(*session);
                    return std::unique_ptr<ISshSession>(std::move(session));
                },
                package);
        }
    }

    ~PluginFixture()
    {
        const QDir run(QDir(sandbox).filePath(QStringLiteral("root/var/run")));
        for (const QString &name : run.entryList({QStringLiteral("spotty-uartd-*.pid")}, QDir::Files)) {
            QFile file(run.filePath(name));
            if (file.open(QIODevice::ReadOnly))
                ::kill(pid_t(file.readAll().trimmed().toLong()), SIGTERM);
        }
    }

    QString dataDirectory() const { return dir.filePath(QStringLiteral("plugin-data/tcptunnel")); }

    /// \brief Настройки подключения с введёнными ssh-данными.
    QVariantMap settings(int port = 7777) const
    {
        QVariantMap values = plugin.settingsSchema().defaults();
        values[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
        values[QStringLiteral("port")] = port;
        values[QStringLiteral("sshUser")] = QStringLiteral("pi");
        values[QStringLiteral("sshPassword")] = QStringLiteral("secret");
        return values;
    }

    QString run(const QString &action, const QVariantMap &values)
    {
        return plugin.triggerAction(device, action, values);
    }

    bool waitDone(int timeoutMs = 30000) { return plugin.tasks()->waitForIdle(timeoutMs); }

    QString status() const
    {
        const auto options = const_cast<TcpTunnelPlugin &>(plugin).liveOptions(device, QStringLiteral("remoteStatus"), {});
        return options.isEmpty() ? QString() : options.first().label;
    }

    /// \brief Как подана строка состояния: итог — успех или сбой, ход — нейтрально.
    NoteSeverity statusSeverity() const
    {
        const auto options = const_cast<TcpTunnelPlugin &>(plugin).liveOptions(device, QStringLiteral("remoteStatus"), {});
        return options.isEmpty() || !options.first().value.isValid() ? NoteSeverity::Neutral
                                                                      : NoteSeverity(options.first().value.toInt());
    }
};

} // namespace

// --- Схема ------------------------------------------------------------------------------

TEST(TcpTunnelInstall, WithoutSshSupportThereAreNoInstallerFieldsAndActionsSayWhy)
{
    PluginFixture f(/*withSsh=*/false);
    EXPECT_FALSE(f.plugin.sshAvailable());
    EXPECT_EQ(f.plugin.settingsSchema().field(QStringLiteral("sshUser")), nullptr);
    EXPECT_EQ(f.plugin.settingsSchema().field(QStringLiteral("install")), nullptr);

    const QString problem = f.run(QStringLiteral("install"), f.settings());
    EXPECT_TRUE(problem.contains(QStringLiteral("no ssh support"))) << qPrintable(problem);
}

TEST(TcpTunnelInstall, SshCredentialsAreNotSettingsAtAll)
{
    PluginFixture f;
    const SettingsSchema schema = f.plugin.settingsSchema();

    // Ни имя, ни пароль, ни ключ не поля настроек подключения: те лежат в файле, а это нужно одному
    // нажатию кнопки. Спрашивает их отдельное окно (promptSchema()).
    for (const char *key : {"sshUser", "sshPassword", "sshKeyFile", "sshKeyPassphrase", "sshPort", "trustNewHost"})
        EXPECT_EQ(schema.field(QString::fromLatin1(key)), nullptr) << key;

    // Ничто из установки не обязательно для открытия канала: подключиться можно и к агенту,
    // поставленному руками.
    EXPECT_EQ(schema.missingRequiredFields(schema.defaults()), QStringList{QStringLiteral("host")});
    EXPECT_TRUE(schema.field(QStringLiteral("remoteUart"))->live);
    EXPECT_TRUE(schema.field(QStringLiteral("remoteUart"))->editable);
    EXPECT_FALSE(schema.field(QStringLiteral("remoteUart"))->required);
    EXPECT_EQ(schema.defaults().value(QStringLiteral("baud")).toInt(), 115200);
    EXPECT_EQ(schema.defaults().value(QStringLiteral("format")).toString(), QStringLiteral("8N1"));
    EXPECT_TRUE(schema.defaults().value(QStringLiteral("autostart")).toBool());
}

TEST(TcpTunnelInstall, TheWindowAsksForUserNameAndPasswordAndHidesTheRest)
{
    PluginFixture f;
    QVariantMap values = f.settings();

    for (const char *action : {"findPorts", "install", "uninstall"}) {
        const SettingsSchema prompt = f.plugin.promptSchema(QString::fromLatin1(action), values);
        ASSERT_FALSE(prompt.isEmpty()) << action;

        ASSERT_NE(prompt.field(QStringLiteral("sshUser")), nullptr);
        EXPECT_TRUE(prompt.field(QStringLiteral("sshUser"))->required);
        EXPECT_FALSE(prompt.field(QStringLiteral("sshUser"))->advanced);
        EXPECT_EQ(prompt.field(QStringLiteral("sshPassword"))->type, SettingsField::Password);
        EXPECT_FALSE(prompt.field(QStringLiteral("sshPassword"))->advanced);

        // Остальное — в «Advanced»: вход по ключу, пароль ключа и порт ssh.
        for (const char *key : {"sshKeyFile", "sshKeyPassphrase", "sshPort"}) {
            ASSERT_NE(prompt.field(QString::fromLatin1(key)), nullptr) << key;
            EXPECT_TRUE(prompt.field(QString::fromLatin1(key))->advanced) << key;
        }
        EXPECT_EQ(prompt.field(QStringLiteral("sshKeyPassphrase"))->type, SettingsField::Password);
        EXPECT_EQ(prompt.defaults().value(QStringLiteral("sshPort")).toInt(), 22);
        // В окне названа машина, на которую входим.
        EXPECT_TRUE(prompt.fields().first().group.contains(QStringLiteral("127.0.0.1"))) << action;
    }

    // Остальным действиям вопрос не нужен.
    EXPECT_TRUE(f.plugin.promptSchema(QStringLiteral("scan"), values).isEmpty());
    EXPECT_TRUE(f.plugin.promptSchema(QStringLiteral("forgetKey"), values).isEmpty());
    EXPECT_TRUE(f.plugin.promptSchema(QStringLiteral("nonsense"), values).isEmpty());
}

TEST(TcpTunnelInstall, ConnectionLayoutPutsThePortBesideTheHostAndHidesTheRareFields)
{
    PluginFixture f;
    const SettingsSchema schema = f.plugin.settingsSchema();

    // Порт — узкое поле справа от адреса; пояснение у него не отдельная строка, а подсказка.
    const QList<SettingsField> connection = schema.fieldsInGroup(QStringLiteral("Connection"));
    ASSERT_GE(connection.size(), 2);
    EXPECT_EQ(connection[0].key, QStringLiteral("host"));
    EXPECT_EQ(connection[1].key, QStringLiteral("port"));
    EXPECT_TRUE(connection[1].compact);
    EXPECT_FALSE(connection[0].compact);

    // Порт поиска — рядом с выбором сети.
    EXPECT_TRUE(schema.field(QStringLiteral("scanPort"))->compact);
    EXPECT_EQ(schema.fieldsInGroup(QStringLiteral("Find on the network")).first().key, QStringLiteral("scanRange"));
    EXPECT_TRUE(schema.field(QStringLiteral("format"))->compact);

    // Редкое спрятано.
    for (const char *key : {"autoReconnect", "connectTimeoutMs", "flow", "bind", "allow", "forgetKey"})
        EXPECT_TRUE(schema.field(QString::fromLatin1(key))->advanced) << key;
    // А то, что нужно при установке каждый раз, — нет.
    for (const char *key : {"host", "port", "remoteUart", "baud", "autostart", "scanRange"})
        EXPECT_FALSE(schema.field(QString::fromLatin1(key))->advanced) << key;
}

TEST(TcpTunnelInstall, ActionsAreLaidOutByTheirImportance)
{
    PluginFixture f;
    const SettingsSchema schema = f.plugin.settingsSchema();
    auto field = [&](const char *key) {
        const SettingsField *found = schema.field(QString::fromLatin1(key));
        EXPECT_NE(found, nullptr) << key;
        return found ? *found : SettingsField{};
    };

    // Главное действие каждой группы выделено, остальные — нет: иначе три одинаковые кнопки подряд.
    EXPECT_TRUE(field("scan").primary);
    EXPECT_TRUE(field("install").primary);
    for (const char *key : {"findPorts", "uninstall", "forgetKey"})
        EXPECT_FALSE(field(key).primary) << key;

    // Поиск портов — справа от списка портов, который он заполняет; удаление — в одном ряду с установкой.
    EXPECT_TRUE(field("findPorts").compact);
    EXPECT_TRUE(field("uninstall").compact);
    EXPECT_FALSE(field("install").compact);
    EXPECT_FALSE(field("scan").compact); // диалог бывает узким: ряд из трёх сжал бы выбор сети

    // Порядок в группе тот, в каком объявлено: порт устройства и его кнопка рядом, итог — после кнопок.
    QStringList keys;
    for (const SettingsField &each : schema.fieldsInGroup(QStringLiteral("Agent on the machine")))
        keys << each.key;
    EXPECT_LT(keys.indexOf(QStringLiteral("remoteUart")), keys.indexOf(QStringLiteral("findPorts")));
    EXPECT_EQ(keys.indexOf(QStringLiteral("findPorts")), keys.indexOf(QStringLiteral("remoteUart")) + 1);
    EXPECT_EQ(keys.indexOf(QStringLiteral("uninstall")), keys.indexOf(QStringLiteral("install")) + 1);
    EXPECT_EQ(keys.last(), QStringLiteral("remoteStatus"));

    // Итог без подписи слева: он говорит сам за себя, а подпись «Status» только сдвигала текст вправо.
    EXPECT_TRUE(field("remoteStatus").label.isEmpty());
    EXPECT_TRUE(field("scanStatus").label.isEmpty());

    // Название кнопки внизу диалога не повторяет «Remove» из его верхней строки: та убирает запись о
    // подключении, а эта — агента с машины.
    EXPECT_FALSE(field("uninstall").label.contains(QStringLiteral("Remove")));
}

TEST(TcpTunnelInstall, ScanNetworkIsChosenFromTheNetworksOfThisComputer)
{
    PluginFixture f;
    const SettingsSchema schema = f.plugin.settingsSchema(); // указатель на поле живёт, пока жива схема
    const SettingsField *range = schema.field(QStringLiteral("scanRange"));
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->type, SettingsField::Choice);
    EXPECT_TRUE(range->live);
    EXPECT_TRUE(range->editable); // свой диапазон набрать можно
    EXPECT_TRUE(range->defaultValue.toString().isEmpty());
    ASSERT_EQ(range->options.size(), 1); // «все сети этого компьютера» — постоянный пункт
    EXPECT_TRUE(range->options.first().value.toString().isEmpty());

    const auto networks = f.plugin.liveOptions(f.device, QStringLiteral("scanRange"), {});
    for (const SettingsOption &option : networks) {
        // Значение — запись, которую понимает сканер; подпись — она же и интерфейс в скобках.
        QList<QHostAddress> targets;
        EXPECT_TRUE(NetworkScanner::parseRange(option.value.toString(), &targets, nullptr)) << qPrintable(option.label);
        EXPECT_FALSE(targets.isEmpty());
        EXPECT_TRUE(option.label.startsWith(option.value.toString()));
        EXPECT_TRUE(option.label.endsWith(QLatin1Char(')')));
    }
}

TEST(TcpTunnelInstall, ScanOfAChosenNetworkOnlyLooksThere)
{
    PluginFixture f;
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    QVariantMap values = f.settings();
    values[QStringLiteral("scanRange")] = QStringLiteral("127.0.0.1/32");
    values[QStringLiteral("scanPort")] = int(server.serverPort());
    ASSERT_TRUE(f.run(QStringLiteral("scan"), values).isEmpty());
    ASSERT_TRUE(waitFor(
        [&] {
            const auto hosts = f.plugin.liveOptions(f.device, QStringLiteral("host"), {});
            return !hosts.isEmpty();
        },
        10000));
    const auto hosts = f.plugin.liveOptions(f.device, QStringLiteral("host"), {});
    ASSERT_EQ(hosts.size(), 1);
    EXPECT_EQ(hosts.first().value.toString(), QStringLiteral("127.0.0.1"));
}

TEST(TcpTunnelInstall, AnAddressTypedByHandGetsItsNameFromDns)
{
    PluginFixture f;
    QVariantMap values = f.settings(7777);
    values[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    EXPECT_TRUE(f.plugin.describeUserDevice(values).description.isEmpty());

    // Опрос полей запускает обратный поиск; ответ приходит через очередь событий.
    f.plugin.liveOptions(f.device, QStringLiteral("host"), values);
    const bool named = waitFor([&] { return !f.plugin.describeUserDevice(values).description.isEmpty(); }, 5000);
    if (!named)
        GTEST_SKIP() << "this machine has no reverse DNS name for 127.0.0.1";
    EXPECT_EQ(f.plugin.describeUserDevice(values).description.section(QLatin1Char(':'), 1), QStringLiteral("7777"));

    // Повторный опрос того же адреса нового запроса не порождает: тест видит это по отсутствию
    // падений и по тому, что имя осталось тем же.
    const QString before = f.plugin.describeUserDevice(values).description;
    f.plugin.liveOptions(f.device, QStringLiteral("host"), values);
    EXPECT_EQ(f.plugin.describeUserDevice(values).description, before);
}

TEST(TcpTunnelInstall, ANameTypedAsTheHostIsNotLookedUp)
{
    PluginFixture f;
    QVariantMap values = f.settings(7777);
    values[QStringLiteral("host")] = QStringLiteral("pi-lab");
    f.plugin.liveOptions(f.device, QStringLiteral("host"), values);
    waitFor([] { return false; }, 200);
    // Имя уже и есть подпись: «pi-lab:7777» через адрес, а описание не нужно.
    EXPECT_TRUE(f.plugin.describeUserDevice(values).description.isEmpty());
    EXPECT_EQ(f.plugin.describeUserDevice(values).systemName, QStringLiteral("pi-lab:7777"));
}

TEST(TcpTunnelInstall, ConnectionIsNamedAfterTheMachineAndTheChosenSerialPort)
{
    PluginFixture f;
    QVariantMap values = f.settings(7778);
    values[QStringLiteral("remoteUart")] = QStringLiteral("/dev/ttyUSB1");
    values[QStringLiteral("baud")] = 921600;
    values[QStringLiteral("format")] = QStringLiteral("8e1");

    // Имя машины ещё не узнали: вместо него адрес, а выжимка настроек добавляет скорость.
    InterfaceDescriptor before = f.plugin.describeUserDevice(values);
    EXPECT_EQ(before.systemName, QStringLiteral("127.0.0.1:7778"));
    EXPECT_EQ(before.description, QStringLiteral("127.0.0.1 ttyUSB1"));
    EXPECT_EQ(f.plugin.settingsSummary(values), QStringLiteral("921600 8E1"));

    // Зашли на машину по ssh — она назвала себя. Подпись теперь «имя порт», адрес остаётся системным именем.
    int notified = 0;
    auto *notifier = static_cast<TcpTunnelDeviceNotifier *>(f.plugin.hotplugNotifier());
    QObject context;
    QObject::connect(notifier, &TcpTunnelDeviceNotifier::devicesChanged, &context, [&notified] { ++notified; });

    f.configure = [](LocalShellSession &session) {
        session.cannedDetect = debianDetect() + QStringLiteral("hostname=raspberrypi\n");
    };
    ASSERT_TRUE(f.run(QStringLiteral("findPorts"), values).isEmpty());
    ASSERT_TRUE(f.waitDone());
    // Имя подхватывается там, где плагин зовут из потока UI, — при очередном опросе полей.
    f.plugin.liveOptions(f.device, QStringLiteral("remoteUart"), values);

    const InterfaceDescriptor after = f.plugin.describeUserDevice(values);
    EXPECT_EQ(after.description, QStringLiteral("raspberrypi ttyUSB1"));
    EXPECT_EQ(after.systemName, QStringLiteral("127.0.0.1:7778"));
    // Реестр узнаёт, что подписи пора пересчитать.
    EXPECT_TRUE(waitFor([&] { return notified > 0; }));
}

// --- Проверка ввода до подключения ------------------------------------------------------

TEST(TcpTunnelInstall, MissingInputIsReportedImmediatelyWithoutConnecting)
{
    PluginFixture f;
    int connections = 0;
    f.configure = [&](LocalShellSession &) { ++connections; };

    QVariantMap noHost = f.settings();
    noHost[QStringLiteral("host")] = QString();
    EXPECT_TRUE(f.run(QStringLiteral("findPorts"), noHost).contains(QStringLiteral("Host field")));

    QVariantMap noUser = f.settings();
    noUser[QStringLiteral("sshUser")] = QString();
    EXPECT_TRUE(f.run(QStringLiteral("findPorts"), noUser).contains(QStringLiteral("ssh user")));

    // Установка без выбранного порта машины подсказывает, как его выбрать.
    EXPECT_TRUE(f.run(QStringLiteral("install"), f.settings()).contains(QStringLiteral("Choose the serial port")));

    QVariantMap badBaud = f.settings();
    badBaud[QStringLiteral("remoteUart")] = QStringLiteral("/dev/ttyUSB0");
    badBaud[QStringLiteral("baud")] = 12345;
    EXPECT_TRUE(f.run(QStringLiteral("install"), badBaud).contains(QStringLiteral("baud"), Qt::CaseInsensitive));

    EXPECT_EQ(connections, 0);
}

// --- Цепочка целиком --------------------------------------------------------------------

TEST(TcpTunnelInstall, FindPortsReportsTheMachineAndTheFirstFreeAgentPort)
{
    PluginFixture f;
    f.configure = [](LocalShellSession &session) {
        session.cannedDetect = debianDetect() + QStringLiteral("instance=7777|/dev/ttyUSB0\nlisten_hex=1E61\n");
    };

    const QString started = f.run(QStringLiteral("findPorts"), f.settings());
    ASSERT_TRUE(started.isEmpty()) << qPrintable(started);
    ASSERT_TRUE(f.waitDone());

    EXPECT_TRUE(f.status().contains(QStringLiteral("Debian GNU/Linux 12"))) << "[" << qPrintable(f.status()) << "]";
    EXPECT_TRUE(f.status().contains(QStringLiteral("Serial ports found")));
    EXPECT_TRUE(f.status().contains(QStringLiteral("First free port for an agent: 7778")));

    // В списке порта подключения — только то, к чему можно подключиться: подсказок «свободный на машине» нет.
    for (const SettingsOption &option : f.plugin.liveOptions(f.device, QStringLiteral("port"), f.settings()))
        EXPECT_EQ(option.label, QString::number(option.value.toInt())) << qPrintable(option.label);
}

TEST(TcpTunnelInstall, FoundSerialPortsAreOfferedWithTheirDescriptions)
{
    PluginFixture f;
    f.configure = [](LocalShellSession &session) {
        session.intercept = [](const QString &command, SshExecResult *result) {
            if (command.endsWith(QStringLiteral(" --scan"))) {
                *result = {0,
                           "/dev/ttyUSB0\t/dev/serial/by-id/usb-FTDI-if00\tFTDI FT232R (0403:6001)\n"
                           "/dev/ttyAMA0\t\tamba-pl011\n",
                           {}};
                return true;
            }
            return false;
        };
    };

    ASSERT_TRUE(f.run(QStringLiteral("findPorts"), f.settings()).isEmpty());
    ASSERT_TRUE(f.waitDone());

    const auto ports = f.plugin.liveOptions(f.device, QStringLiteral("remoteUart"), {});
    ASSERT_EQ(ports.size(), 2);
    EXPECT_EQ(ports[0].value.toString(), QStringLiteral("/dev/ttyUSB0"));
    EXPECT_EQ(ports[0].label, QStringLiteral("ttyUSB0  -  FTDI FT232R (0403:6001)"));
    EXPECT_EQ(ports[1].label, QStringLiteral("ttyAMA0  -  amba-pl011"));
}

TEST(TcpTunnelInstall, InstallThroughThePluginLeavesAWorkingAgent)
{
    PluginFixture f;
    FakeUart uart;
    const quint16 port = freePort();

    QVariantMap values = f.settings(port);
    values[QStringLiteral("remoteUart")] = uart.path();
    values[QStringLiteral("baud")] = 57600;
    values[QStringLiteral("bind")] = QStringLiteral("127.0.0.1");
    values[QStringLiteral("autostart")] = false; // песочница без systemd
    ASSERT_TRUE(f.run(QStringLiteral("install"), values).isEmpty());
    ASSERT_TRUE(f.waitDone());

    EXPECT_TRUE(f.status().startsWith(QStringLiteral("Installed: the agent listens on 127.0.0.1:%1").arg(port)))
        << qPrintable(f.status());
    EXPECT_EQ(f.statusSeverity(), NoteSeverity::Ok);

    // Настроенный порт и скорость дошли до агента, байты ходят.
    struct termios tio;
    ASSERT_TRUE(waitFor([&] { return uart.termios(&tio) && cfgetispeed(&tio) == speed_t(B57600); }));
    Client client(port);
    ASSERT_TRUE(client.connected);
    // Агент получил байты из порта раньше, чем принял клиента, — они пропали бы: клиентов у него
    // ещё нет. Журнал запущенного скриптом агента подскажет, что клиент принят.
    const QString agentLog = QDir(f.sandbox).filePath(QStringLiteral("root/var/log/spotty-uartd-%1.log").arg(port));
    ASSERT_TRUE(waitFor(
        [&] {
            QFile log(agentLog);
            return log.open(QIODevice::ReadOnly) && log.readAll().contains("connected (1 total)");
        },
        10000));
    uart.emitBytes("ok");
    ASSERT_TRUE(client.waitForBytes(2));
    EXPECT_EQ(client.received, QByteArray("ok"));
}

TEST(TcpTunnelInstall, FailureShowsTheReasonInTheStatusAndNothingIsStored)
{
    PluginFixture f;
    f.configure = [](LocalShellSession &session) { session.sudoPassword = QStringLiteral("other"); };
    FakeUart uart;
    QVariantMap values = f.settings(freePort());
    values[QStringLiteral("remoteUart")] = uart.path();
    values[QStringLiteral("autostart")] = false;

    ASSERT_TRUE(f.run(QStringLiteral("install"), values).isEmpty());
    ASSERT_TRUE(f.waitDone());

    EXPECT_TRUE(f.status().contains(QStringLiteral("sudo did not accept the password"))) << qPrintable(f.status());
    EXPECT_FALSE(f.status().contains(QStringLiteral("secret"))); // пароль не попадает в сообщения
    EXPECT_EQ(f.statusSeverity(), NoteSeverity::Error);
}

TEST(TcpTunnelInstall, UninstallRemovesTheAgentAndTellsWhatToDoNext)
{
    PluginFixture f;
    FakeUart uart;
    const quint16 port = freePort();
    QVariantMap values = f.settings(port);
    values[QStringLiteral("remoteUart")] = uart.path();
    values[QStringLiteral("bind")] = QStringLiteral("127.0.0.1");
    values[QStringLiteral("autostart")] = false;
    ASSERT_TRUE(f.run(QStringLiteral("install"), values).isEmpty());
    ASSERT_TRUE(f.waitDone());

    f.configure = [&](LocalShellSession &session) {
        session.cannedDetect = debianDetect() + QStringLiteral("instance=%1|%2\n").arg(port).arg(uart.path());
    };
    ASSERT_TRUE(f.run(QStringLiteral("uninstall"), values).isEmpty());
    ASSERT_TRUE(f.waitDone());

    EXPECT_TRUE(f.status().contains(QStringLiteral("was removed"))) << qPrintable(f.status());
    EXPECT_TRUE(f.status().contains(QStringLiteral("remove this connection")));
    EXPECT_EQ(f.statusSeverity(), NoteSeverity::Ok);
    EXPECT_FALSE(QFile::exists(QDir(f.sandbox).filePath(QStringLiteral("root/etc/spotty-uartd/%1.conf").arg(port))));
}

TEST(TcpTunnelInstall, SecondOperationWhileOneRunsIsRefused)
{
    PluginFixture f;
    std::atomic<bool> release{false};
    f.configure = [&](LocalShellSession &session) {
        session.intercept = [&release](const QString &command, SshExecResult *result) {
            if (command == QLatin1String("sh -s")) {
                while (!release)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                *result = {0, debianDetect().toUtf8(), {}};
                return true;
            }
            return false;
        };
    };

    ASSERT_TRUE(f.run(QStringLiteral("findPorts"), f.settings()).isEmpty());
    EXPECT_TRUE(f.run(QStringLiteral("findPorts"), f.settings()).contains(QStringLiteral("still running")));
    // Другому подключению это не мешает.
    f.device.id = QStringLiteral("tcptunnel:user:other");
    EXPECT_TRUE(f.run(QStringLiteral("findPorts"), f.settings()).isEmpty());

    release = true;
    ASSERT_TRUE(f.waitDone());
}

TEST(TcpTunnelInstall, UnknownHostKeyIsShownFirstAndTrustedByRepeatingTheAction)
{
    PluginFixture f(/*withSsh=*/true, /*hostKeyKnown=*/false);
    const QString dataDir = f.dataDirectory();
    f.plugin.setDataDirectory(dataDir);

    // Первое нажатие: ключа Spotty не знает, пароль не отправлен, отпечаток показан.
    ASSERT_TRUE(f.run(QStringLiteral("findPorts"), f.settings()).isEmpty());
    ASSERT_TRUE(f.waitDone());
    EXPECT_TRUE(f.status().contains(QStringLiteral("not known yet"))) << qPrintable(f.status());
    EXPECT_TRUE(f.status().contains(QStringLiteral("SHA256:testfingerprint")));
    EXPECT_TRUE(f.status().contains(QStringLiteral("press the same button again")));
    EXPECT_FALSE(QFile::exists(dataDir + QStringLiteral("/known_hosts")));

    // Повтор — подтверждение того, что показали: ключ запомнен, работа идёт.
    ASSERT_TRUE(f.run(QStringLiteral("findPorts"), f.settings()).isEmpty());
    ASSERT_TRUE(f.waitDone());
    EXPECT_TRUE(f.status().contains(QStringLiteral("Serial ports found"))) << qPrintable(f.status());
    EXPECT_TRUE(QFile::exists(dataDir + QStringLiteral("/known_hosts")));

    // Дальше вопросов нет.
    ASSERT_TRUE(f.run(QStringLiteral("findPorts"), f.settings()).isEmpty());
    ASSERT_TRUE(f.waitDone());
    EXPECT_TRUE(f.status().contains(QStringLiteral("Serial ports found")));
}

TEST(TcpTunnelInstall, ConfirmationDoesNotCarryOverToAnotherKey)
{
    PluginFixture f(/*withSsh=*/true, /*hostKeyKnown=*/false);
    f.plugin.setDataDirectory(f.dataDirectory());
    ASSERT_TRUE(f.run(QStringLiteral("findPorts"), f.settings()).isEmpty());
    ASSERT_TRUE(f.waitDone());
    ASSERT_TRUE(f.status().contains(QStringLiteral("SHA256:testfingerprint")));

    // Между нажатиями хост предъявил другой ключ: подтверждён был не он.
    f.configure = [](LocalShellSession &session) { session.fingerprint = QStringLiteral("SHA256:someoneElse"); };
    ASSERT_TRUE(f.run(QStringLiteral("findPorts"), f.settings()).isEmpty());
    ASSERT_TRUE(f.waitDone());

    EXPECT_TRUE(f.status().contains(QStringLiteral("SHA256:someoneElse"))) << qPrintable(f.status());
    EXPECT_TRUE(f.status().contains(QStringLiteral("not known yet")));
    EXPECT_FALSE(QFile::exists(f.dataDirectory() + QStringLiteral("/known_hosts")));
}

TEST(TcpTunnelInstall, ForgettingTheKeyBringsTheQuestionBack)
{
    PluginFixture f; // ключ известен
    QVariantMap values = f.settings();

    EXPECT_TRUE(f.run(QStringLiteral("forgetKey"), values).isEmpty());
    EXPECT_TRUE(f.run(QStringLiteral("forgetKey"), values).contains(QStringLiteral("no saved ssh key")));

    ASSERT_TRUE(f.run(QStringLiteral("findPorts"), values).isEmpty());
    ASSERT_TRUE(f.waitDone());
    EXPECT_TRUE(f.status().contains(QStringLiteral("not known yet"))) << qPrintable(f.status());
}
