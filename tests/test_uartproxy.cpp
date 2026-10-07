/**
 * \file test_uartproxy.cpp
 * \brief Тесты перехватчика порта: spotty::PtyPort и spotty::UartProxyChannel.
 *
 * Устройство изображает ещё один псевдотерминал, поэтому проверка идёт настоящими
 * дескрипторами и без железа: «программа» пишет в ссылку на виртуальный порт, «устройство»
 * читает и отвечает через ведущий конец своего pty.
 */
#include "support/TestSupport.h"

#include <DevLink.h>
#include <PtyPort.h>
#include <UartProxyChannel.h>
#include <UartProxySettings.h>

#include <QFile>
#include <QFileInfo>
#include <QList>

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdlib>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

using namespace spotty;
using spotty::test::TempDir;
using spotty::test::waitFor;

namespace {

/**
 * \class FakeDevice
 * \brief Псевдотерминал, изображающий реальный порт: его подчинённый конец открывает
 *        перехватчик, ведущим пользуется тест.
 */
class FakeDevice
{
public:
    FakeDevice()
    {
        m_master = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (m_master < 0 || ::grantpt(m_master) != 0 || ::unlockpt(m_master) != 0)
            return;
        m_slavePath = QString::fromLocal8Bit(::ptsname(m_master));
        ::fcntl(m_master, F_SETFL, ::fcntl(m_master, F_GETFL) | O_NONBLOCK);
    }
    ~FakeDevice()
    {
        if (m_master >= 0)
            ::close(m_master);
    }

    bool isValid() const { return m_master >= 0 && !m_slavePath.isEmpty(); }
    QString slavePath() const { return m_slavePath; }

    /// \brief Что устройство получило к этому моменту; копится между вызовами.
    QByteArray received()
    {
        char buffer[512];
        for (;;) {
            const ssize_t count = ::read(m_master, buffer, sizeof(buffer));
            if (count <= 0)
                break;
            m_received.append(buffer, int(count));
        }
        return m_received;
    }

    void send(const QByteArray &data) { [[maybe_unused]] auto n = ::write(m_master, data.constData(), size_t(data.size())); }

private:
    int m_master = -1;
    QString m_slavePath;
    QByteArray m_received;
};

/// \brief «Чужая программа»: открывает путь как последовательный порт, как это делает любая.
class OtherProgram
{
public:
    explicit OtherProgram(const QString &path)
    {
        m_fd = ::open(path.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (m_fd < 0)
            return;
        termios attributes{};
        if (::tcgetattr(m_fd, &attributes) == 0) {
            ::cfmakeraw(&attributes);
            ::tcsetattr(m_fd, TCSANOW, &attributes);
        }
    }
    ~OtherProgram()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }

    bool isOpen() const { return m_fd >= 0; }

    void send(const QByteArray &data) { [[maybe_unused]] auto n = ::write(m_fd, data.constData(), size_t(data.size())); }

    QByteArray received()
    {
        char buffer[512];
        for (;;) {
            const ssize_t count = ::read(m_fd, buffer, sizeof(buffer));
            if (count <= 0)
                break;
            m_received.append(buffer, int(count));
        }
        return m_received;
    }

private:
    int m_fd = -1;
    QByteArray m_received;
};

/// \brief Собирает порции из сигнала канала вместе с их метками времени.
struct Chunks
{
    struct Item
    {
        QByteArray data;
        qint64 ns = 0;
    };
    QList<Item> items;

    int count() const { return int(items.size()); }
    const Item &first() const { return items.first(); }
    void add(const QByteArray &data, qint64 ns) { items.append({data, ns}); }
};

QVariantMap proxySettings(const QString &realPort, const QString &linkPath)
{
    namespace k = uartproxy;
    return {
        {QLatin1String(k::kRealPort), realPort},
        {QLatin1String(k::kBaudRate), 115200},
        {QLatin1String(k::kDataBits), 8},
        {QLatin1String(k::kParity), QStringLiteral("N")},
        {QLatin1String(k::kStopBits), QStringLiteral("1")},
        {QLatin1String(k::kFlowControl), QStringLiteral("none")},
        {QLatin1String(k::kDtrOnOpen), false},
        {QLatin1String(k::kRtsOnOpen), false},
        {QLatin1String(k::kLinkPath), linkPath},
        {QLatin1String(k::kVirtualPort), QString()},
    };
}

} // namespace

TEST(PtyPort, ProgramSeesALinkToTheVirtualPort)
{
    TempDir dir;
    const QString link = dir.filePath(QStringLiteral("tty"));

    PtyPort port(link);
    QString error;
    ASSERT_TRUE(port.open({}, &error)) << qPrintable(error);

    EXPECT_EQ(port.description(), link);
    EXPECT_TRUE(QFileInfo(link).isSymLink());
    EXPECT_EQ(QFileInfo(link).symLinkTarget(), port.slavePath());

    port.close();
    // Висячая ссылка на исчезнувший порт хуже её отсутствия: программа открыла бы её и
    // получила бы непонятную ошибку вместо «нет такого файла».
    EXPECT_FALSE(QFileInfo(link).isSymLink());
}

TEST(PtyPort, DoesNotOverwriteAForeignFile)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("precious"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("data");
    file.close();

    PtyPort port(path);
    QString error;
    EXPECT_FALSE(port.open({}, &error));
    EXPECT_FALSE(error.isEmpty());

    QFile after(path);
    ASSERT_TRUE(after.open(QIODevice::ReadOnly));
    EXPECT_EQ(after.readAll(), QByteArrayLiteral("data"));
}

TEST(PtyPort, ReplacesALeftoverLinkFromAPreviousRun)
{
    TempDir dir;
    const QString link = dir.filePath(QStringLiteral("tty"));
    ASSERT_TRUE(QFile::link(QStringLiteral("/nonexistent"), link));

    PtyPort port(link);
    QString error;
    ASSERT_TRUE(port.open({}, &error)) << qPrintable(error);
    EXPECT_EQ(QFileInfo(link).symLinkTarget(), port.slavePath());
}

TEST(PtyPort, PassesBytesBothWays)
{
    PtyPort port{QString()};
    QString error;
    ASSERT_TRUE(port.open({}, &error)) << qPrintable(error);

    QByteArray fromProgram;
    QObject::connect(&port, &VirtualPort::dataRead,
                     [&](const QByteArray &data) { fromProgram.append(data); });

    OtherProgram program(port.slavePath());
    ASSERT_TRUE(program.isOpen());

    program.send(QByteArrayLiteral("hello"));
    ASSERT_TRUE(waitFor([&] { return fromProgram == QByteArrayLiteral("hello"); }));

    port.write(QByteArrayLiteral("world"));
    ASSERT_TRUE(waitFor([&] { return program.received() == QByteArrayLiteral("world"); }));
}

TEST(PtyPort, DoesNotEchoWhatItWrites)
{
    // Без «сырого» режима драйвер терминала возвращал бы записанное в ведущий конец обратно
    // читателю ведущего конца: ответ устройства стал бы «отправкой программы».
    PtyPort port{QString()};
    QString error;
    ASSERT_TRUE(port.open({}, &error)) << qPrintable(error);

    QByteArray fromProgram;
    QObject::connect(&port, &VirtualPort::dataRead,
                     [&](const QByteArray &data) { fromProgram.append(data); });

    port.write(QByteArrayLiteral("device says hi\r\n"));
    QCoreApplication::processEvents();
    waitFor([&] { return !fromProgram.isEmpty(); }, 200);

    EXPECT_TRUE(fromProgram.isEmpty());
}

TEST(UartProxyChannel, ForwardsAndReportsBothDirections)
{
    FakeDevice device;
    ASSERT_TRUE(device.isValid());
    TempDir dir;
    const QString link = dir.filePath(QStringLiteral("tty"));

    UartProxyChannel channel;
    Chunks received;
    Chunks transmitted;
    QObject::connect(&channel, &IInterfaceChannel::dataReceived,
                     [&](const QByteArray &d, qint64 ns) { received.add(d, ns); });
    QObject::connect(&channel, &IInterfaceChannel::dataTransmitted,
                     [&](const QByteArray &d, qint64 ns) { transmitted.add(d, ns); });

    QString error;
    ASSERT_TRUE(channel.open(proxySettings(device.slavePath(), link), &error))
        << qPrintable(error);
    EXPECT_EQ(channel.state(), ChannelState::Open);

    OtherProgram program(link);
    ASSERT_TRUE(program.isOpen());

    // Программа → устройство: устройство получает, а Spotty видит передачу.
    program.send(QByteArrayLiteral("AT\r"));
    ASSERT_TRUE(waitFor([&] { return device.received() == QByteArrayLiteral("AT\r"); }));
    ASSERT_TRUE(waitFor([&] { return transmitted.count() >= 1; }));
    EXPECT_EQ(transmitted.first().data, QByteArrayLiteral("AT\r"));

    // Устройство → программа: программа получает, а Spotty видит приём.
    device.send(QByteArrayLiteral("OK\r\n"));
    ASSERT_TRUE(waitFor([&] { return program.received() == QByteArrayLiteral("OK\r\n"); }));
    ASSERT_TRUE(waitFor([&] { return received.count() >= 1; }));
    EXPECT_EQ(received.first().data, QByteArrayLiteral("OK\r\n"));

    // Порядок направлений — содержание перехвата: отметки идут одними часами.
    EXPECT_LE(transmitted.first().ns, received.first().ns);
}

TEST(UartProxyChannel, DoesNotMistakeOwnForwardingForProgramTraffic)
{
    FakeDevice device;
    ASSERT_TRUE(device.isValid());
    TempDir dir;
    const QString link = dir.filePath(QStringLiteral("tty"));

    UartProxyChannel channel;
    Chunks transmitted;
    QObject::connect(&channel, &IInterfaceChannel::dataTransmitted,
                     [&](const QByteArray &d, qint64 ns) { transmitted.add(d, ns); });
    QString error;
    ASSERT_TRUE(channel.open(proxySettings(device.slavePath(), link), &error))
        << qPrintable(error);

    // Только устройство говорит, программа молчит: передачи быть не должно. Иначе ответ
    // вернулся бы с виртуального порта как «отправка» и ушёл устройству обратно.
    device.send(QByteArrayLiteral("telemetry\r\n"));
    QCoreApplication::processEvents();
    waitFor([&] { return transmitted.count() > 0; }, 300);

    EXPECT_EQ(transmitted.count(), 0);
}

TEST(UartProxyChannel, WriteGoesToTheDeviceOnly)
{
    FakeDevice device;
    ASSERT_TRUE(device.isValid());
    TempDir dir;
    const QString link = dir.filePath(QStringLiteral("tty"));

    UartProxyChannel channel;
    QString error;
    ASSERT_TRUE(channel.open(proxySettings(device.slavePath(), link), &error))
        << qPrintable(error);
    OtherProgram program(link);

    EXPECT_EQ(channel.write(QByteArrayLiteral("from-spotty")), 11);
    ASSERT_TRUE(waitFor([&] { return device.received() == QByteArrayLiteral("from-spotty"); }));

    // Программе вставка Spotty не показывается: она увидела бы чужой ответ на свой запрос.
    QCoreApplication::processEvents();
    EXPECT_TRUE(program.received().isEmpty());
}

TEST(UartProxyChannel, CloseRemovesTheLinkAndIsRepeatable)
{
    FakeDevice device;
    ASSERT_TRUE(device.isValid());
    TempDir dir;
    const QString link = dir.filePath(QStringLiteral("tty"));

    UartProxyChannel channel;
    QString error;
    ASSERT_TRUE(channel.open(proxySettings(device.slavePath(), link), &error))
        << qPrintable(error);
    ASSERT_TRUE(QFileInfo(link).isSymLink());

    channel.close();
    channel.close();
    EXPECT_EQ(channel.state(), ChannelState::Closed);
    EXPECT_FALSE(QFileInfo(link).isSymLink());
}

TEST(UartProxyChannel, RefusesToOpenWithoutARealPort)
{
    UartProxyChannel channel;
    QString error;
    EXPECT_FALSE(channel.open(proxySettings(QString(), QString()), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(channel.state(), ChannelState::Error);
}

TEST(UartProxyChannel, FailsCleanlyWhenTheRealPortIsMissing)
{
    TempDir dir;
    const QString link = dir.filePath(QStringLiteral("tty"));

    UartProxyChannel channel;
    QString error;
    EXPECT_FALSE(channel.open(
        proxySettings(QStringLiteral("/dev/spotty-no-such-port"), link), &error));
    EXPECT_FALSE(error.isEmpty());
    // Виртуальный порт без реального создавать не за чем, и ссылке неоткуда взяться.
    EXPECT_FALSE(QFileInfo(link).isSymLink());
}

TEST(UartProxyChannel, SpeedCanChangeWithoutReopeningButPortsCannot)
{
    FakeDevice device;
    ASSERT_TRUE(device.isValid());
    TempDir dir;
    const QString link = dir.filePath(QStringLiteral("tty"));

    UartProxyChannel channel;
    QString error;
    QVariantMap settings = proxySettings(device.slavePath(), link);
    ASSERT_TRUE(channel.open(settings, &error)) << qPrintable(error);

    settings[QLatin1String(uartproxy::kBaudRate)] = 57600;
    EXPECT_TRUE(channel.applySettings(settings));

    settings[QLatin1String(uartproxy::kLinkPath)] = dir.filePath(QStringLiteral("other"));
    EXPECT_FALSE(channel.applySettings(settings));
}

namespace {

/// \brief Подменяет исполнителя с повышением прав и бросает его обратно по выходе из теста.
class FakeElevator
{
public:
    explicit FakeElevator(bool succeed = true, bool decline = false)
    {
        uartproxy::setElevatorForTesting(
            [this, succeed, decline](const QString &script, bool *cancelled, QString *error) {
                ++calls;
                lastScript = script;
                if (!probe.isEmpty())
                    probeExisted = QFileInfo(probe).isSymLink();
                if (decline) {
                    if (cancelled)
                        *cancelled = true;
                    return false;
                }
                if (!succeed && error)
                    *error = QStringLiteral("refused by test");
                return succeed;
            });
    }
    ~FakeElevator() { uartproxy::setElevatorForTesting({}); }

    int calls = 0;
    QString lastScript;

    /// \brief Путь, наличие которого (как ссылки) запоминается в момент вызова.
    QString probe;
    bool probeExisted = false;
};

} // namespace

TEST(DevLink, OnlyPlainNamesDirectlyInDevAreAccepted)
{
    using uartproxy::isDevicePath;
    using uartproxy::isValidDevicePath;

    EXPECT_TRUE(isDevicePath(QStringLiteral("/dev/cu.spotty")));
    EXPECT_FALSE(isDevicePath(QStringLiteral("/tmp/spotty-uart")));

    EXPECT_TRUE(isValidDevicePath(QStringLiteral("/dev/cu.spotty")));
    EXPECT_TRUE(isValidDevicePath(QStringLiteral("/dev/ttyV0")));

    // Путь уходит в команду, которую исполнит root: всё, что не простое имя, отсекается до
    // запуска, а не экранируется в расчёте, что экранирование не подведёт.
    EXPECT_FALSE(isValidDevicePath(QStringLiteral("/dev/")));
    EXPECT_FALSE(isValidDevicePath(QStringLiteral("/dev/sub/dir")));
    EXPECT_FALSE(isValidDevicePath(QStringLiteral("/dev/../etc/passwd")));
    EXPECT_FALSE(isValidDevicePath(QStringLiteral("/dev/..")));
    EXPECT_FALSE(isValidDevicePath(QStringLiteral("/dev/a b")));
    EXPECT_FALSE(isValidDevicePath(QStringLiteral("/dev/a'; rm -rf /; '")));
    EXPECT_FALSE(isValidDevicePath(QStringLiteral("/dev/a\"b")));
    EXPECT_FALSE(isValidDevicePath(QStringLiteral("/dev/a\nb")));
    EXPECT_FALSE(isValidDevicePath(QStringLiteral("/dev/-rf")));
}

TEST(DevLink, InspectDistinguishesMissingReadyAndForeign)
{
    TempDir dir;
    const QString target = dir.filePath(QStringLiteral("bridge"));
    const QString link = dir.filePath(QStringLiteral("link"));

    EXPECT_EQ(uartproxy::inspectLink(link, target), uartproxy::LinkState::Missing);

    ASSERT_TRUE(QFile::link(target, link));
    EXPECT_EQ(uartproxy::inspectLink(link, target), uartproxy::LinkState::Ready);
    EXPECT_EQ(uartproxy::inspectLink(link, dir.filePath(QStringLiteral("other"))),
              uartproxy::LinkState::Foreign);

    // Не ссылка вовсе — тоже чужое.
    const QString file = dir.filePath(QStringLiteral("file"));
    QFile plain(file);
    ASSERT_TRUE(plain.open(QIODevice::WriteOnly));
    plain.close();
    EXPECT_EQ(uartproxy::inspectLink(file, target), uartproxy::LinkState::Foreign);
}

TEST(DevLink, CreatesTheLinkThroughOneElevatedCommand)
{
    TempDir dir;
    uartproxy::setBridgePathForTesting(dir.filePath(QStringLiteral("bridge")));
    FakeElevator elevator(/*succeed=*/true);

    QString error;
    // Исполнитель «успешен», но ссылки в /dev не появилось: настоящая проверка после него
    // обязана это заметить, а не поверить коду возврата.
    EXPECT_FALSE(uartproxy::ensureDeviceLink(QStringLiteral("/dev/spotty-test-missing"), &error));
    EXPECT_EQ(elevator.calls, 1);
    EXPECT_NE(error.indexOf(QStringLiteral("was not created")), -1);

    EXPECT_NE(elevator.lastScript.indexOf(QStringLiteral("/bin/ln -s")), -1);
    EXPECT_NE(elevator.lastScript.indexOf(QStringLiteral("/dev/spotty-test-missing")), -1);
    EXPECT_NE(elevator.lastScript.indexOf(dir.filePath(QStringLiteral("bridge"))), -1);

    uartproxy::setBridgePathForTesting({});
}

TEST(DevLink, DecliningThePasswordIsReportedAsSuch)
{
    FakeElevator elevator(/*succeed=*/false, /*decline=*/true);

    QString error;
    EXPECT_FALSE(uartproxy::ensureDeviceLink(QStringLiteral("/dev/spotty-test-missing"), &error));
    EXPECT_NE(error.indexOf(QStringLiteral("Administrator rights")), -1);
}

TEST(DevLink, NeverAsksForRightsOnAnInvalidOrForeignName)
{
    FakeElevator elevator;

    QString error;
    EXPECT_FALSE(uartproxy::ensureDeviceLink(QStringLiteral("/dev/a b"), &error));
    // /dev/null существует и ссылкой на промежуточную не является: чужое.
    EXPECT_FALSE(uartproxy::ensureDeviceLink(QStringLiteral("/dev/null"), &error));
    EXPECT_FALSE(uartproxy::removeDeviceLink(QStringLiteral("/dev/null"), &error));

    // Удалять нечего — права и тут не нужны.
    EXPECT_TRUE(uartproxy::removeDeviceLink(QStringLiteral("/dev/spotty-test-missing"), &error));

    EXPECT_EQ(elevator.calls, 0);
}

TEST(PtyPort, RefusesAForeignNameInDevWithoutAskingForRights)
{
    FakeElevator elevator;
    TempDir dir;
    uartproxy::setBridgePathForTesting(dir.filePath(QStringLiteral("bridge")));

    PtyPort port(QStringLiteral("/dev/null"));
    QString error;
    EXPECT_FALSE(port.open({}, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(elevator.calls, 0);
    // Промежуточной ссылке тоже не появляться: порт не открыт, и вести ей не на что.
    EXPECT_FALSE(QFileInfo(dir.filePath(QStringLiteral("bridge"))).isSymLink());

    uartproxy::setBridgePathForTesting({});
}

#ifdef Q_OS_MACOS
TEST(PtyPort, MacOsRefusesANameInDevWithoutAskingForAPassword)
{
    // devfs на macOS не принимает чужих имён даже у администратора: `ln` отвечает
    // «Operation not permitted». Просить пароль ради заведомого отказа нельзя.
    FakeElevator elevator;
    TempDir dir;
    uartproxy::setBridgePathForTesting(dir.filePath(QStringLiteral("bridge")));

    PtyPort port(QStringLiteral("/dev/cu.spotty-test"));
    QString error;
    EXPECT_FALSE(port.open({}, &error));
    EXPECT_EQ(elevator.calls, 0);
    EXPECT_NE(error.indexOf(QStringLiteral("/dev")), -1);
    EXPECT_FALSE(QFileInfo(dir.filePath(QStringLiteral("bridge"))).isSymLink());

    uartproxy::setBridgePathForTesting({});
}
#else
TEST(PtyPort, CreatesTheBridgeBeforeAskingForRightsAndRemovesItOnClose)
{
    FakeElevator elevator(/*succeed=*/true);
    TempDir dir;
    const QString bridge = dir.filePath(QStringLiteral("bridge"));
    uartproxy::setBridgePathForTesting(bridge);

    elevator.probe = bridge;

    PtyPort port(QStringLiteral("/dev/spotty-test-missing"));
    QString error;
    // Ссылки в /dev исполнитель не создал, и открытие отказывает, — но промежуточная к
    // этому моменту уже была, и закрытие её убирает: после отказа ничего не остаётся.
    EXPECT_FALSE(port.open({}, &error));
    EXPECT_EQ(elevator.calls, 1);
    // К моменту запроса прав промежуточная ссылка уже лежала: иначе ссылка в /dev, созданная
    // с правами, указывала бы в никуда.
    EXPECT_TRUE(elevator.probeExisted);
    EXPECT_FALSE(QFileInfo(bridge).isSymLink());

    uartproxy::setBridgePathForTesting({});
}
#endif
