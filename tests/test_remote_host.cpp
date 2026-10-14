/**
 * \file test_remote_host.cpp
 * \brief Тесты разбора сведений об удалённой машине, выбора агента и параметров установки.
 */
#include "HostNames.h"
#include "KnownHosts.h"
#include "RemoteHost.h"
#include "TcpTunnelEndpoint.h"

#include "support/TestSupport.h"

#include <QProcess>
#include <QRegularExpression>

#include <gtest/gtest.h>

#include <functional>

using namespace spotty::tcptunnel;
using spotty::test::TempDir;

namespace {

/// \brief Вывод detect.sh для типовых машин. Собран по образцу настоящего вывода.
QString detectOutput(const QString &uname, const QString &dpkg, const QString &osName,
                     bool systemd = true, const QString &extra = {}, const QString &elf = {})
{
    return QStringLiteral("uname_s=Linux\nuname_m=%1\ndpkg_arch=%2\nlong_bit=%3\nuid=1000\nelf=%4\n"
                          "os_name=%5\nsystemd=%6\nsudo=yes\n%7")
        .arg(uname, dpkg)
        .arg(uname.contains(QStringLiteral("64")) ? 64 : 32)
        .arg(elf, osName, systemd ? QStringLiteral("yes") : QStringLiteral("no"), extra);
}

/// \brief Заголовок ELF (40 байт) 32-битного ARM с заданными флагами e_flags.
QString armElf(quint32 flags)
{
    QByteArray header(40, '\0');
    header.replace(0, 4, QByteArray("\x7f" "ELF", 4));
    header[4] = 1;  // 32 бита
    header[5] = 1;  // little-endian
    header[18] = 0x28; // EM_ARM
    for (int i = 0; i < 4; ++i)
        header[36 + i] = char((flags >> (8 * i)) & 0xFF);
    return QString::fromLatin1(header.toHex());
}

} // namespace

// --- Выбор агента -----------------------------------------------------------------------

TEST(RemoteHostArch, DebianFamilyIsDecidedByPackageArchitecture)
{
    struct Case { const char *uname; const char *dpkg; const char *expected; };
    const Case cases[] = {
        {"x86_64", "amd64", "x86_64"},
        {"i686", "i386", "i386"},
        {"aarch64", "arm64", "aarch64"},
        {"armv7l", "armhf", "armv7"},
        {"armv6l", "armhf", "armv6"},   // Raspberry Pi Zero и 1
        {"armv7l", "armel", "armel"},
        {"riscv64", "riscv64", "riscv64"},
        // 32-битный Raspberry Pi OS на ядре Pi 4: uname говорит aarch64, а исполняться должен armhf.
        {"aarch64", "armhf", "armv7"},
    };
    for (const Case &c : cases) {
        const HostInfo info = parseHostInfo(detectOutput(QString::fromLatin1(c.uname),
                                                         QString::fromLatin1(c.dpkg), QStringLiteral("Debian")));
        EXPECT_EQ(info.agentArch, QString::fromLatin1(c.expected)) << c.uname << "/" << c.dpkg;
        EXPECT_TRUE(info.archProblem.isEmpty()) << c.uname << "/" << c.dpkg << ": " << qPrintable(info.archProblem);
    }
}

TEST(RemoteHostArch, WithoutDpkgItFallsBackToUnameAndTheElfHeader)
{
    // Alpine на x86_64.
    EXPECT_EQ(parseHostInfo(detectOutput(QStringLiteral("x86_64"), {}, QStringLiteral("Alpine"))).agentArch,
              QStringLiteral("x86_64"));

    // 32-битный userland на x86_64-ядре.
    HostInfo info = parseHostInfo(detectOutput(QStringLiteral("x86_64"), {}, QStringLiteral("x")));
    info.longBit = 32;
    chooseAgentArch(&info);
    EXPECT_EQ(info.agentArch, QStringLiteral("i386"));

    // ARM: жёсткий float (EF_ARM_ABI_FLOAT_HARD) — armhf, иначе armel.
    constexpr quint32 kHardFloat = 0x05000400; // EABI5 + hard float
    constexpr quint32 kSoftFloat = 0x05000200;
    EXPECT_EQ(parseHostInfo(detectOutput(QStringLiteral("armv7l"), {}, QStringLiteral("x"), true, {},
                                         armElf(kHardFloat))).agentArch,
              QStringLiteral("armv7"));
    EXPECT_EQ(parseHostInfo(detectOutput(QStringLiteral("armv7l"), {}, QStringLiteral("x"), true, {},
                                         armElf(kSoftFloat))).agentArch,
              QStringLiteral("armel"));
    EXPECT_EQ(parseHostInfo(detectOutput(QStringLiteral("armv6l"), {}, QStringLiteral("x"), true, {},
                                         armElf(kHardFloat))).agentArch,
              QStringLiteral("armv6"));
    // Заголовок не прочитан — считаем самый частый случай: жёсткий float.
    EXPECT_EQ(parseHostInfo(detectOutput(QStringLiteral("armv7l"), {}, QStringLiteral("x"))).agentArch,
              QStringLiteral("armv7"));
}

TEST(RemoteHostArch, UnsupportedMachinesGetAnExplanationInsteadOfAWrongBinary)
{
    HostInfo mac = parseHostInfo(QStringLiteral("uname_s=Darwin\nuname_m=arm64\n"));
    EXPECT_TRUE(mac.agentArch.isEmpty());
    EXPECT_TRUE(mac.archProblem.contains(QStringLiteral("Darwin")));

    HostInfo armv5 = parseHostInfo(detectOutput(QStringLiteral("armv5tel"), {}, QStringLiteral("x")));
    EXPECT_TRUE(armv5.agentArch.isEmpty());
    EXPECT_TRUE(armv5.archProblem.contains(QStringLiteral("ARMv5")));

    HostInfo mips = parseHostInfo(detectOutput(QStringLiteral("mips"), {}, QStringLiteral("x")));
    EXPECT_TRUE(mips.archProblem.contains(QStringLiteral("mips")));

    HostInfo power = parseHostInfo(detectOutput(QStringLiteral("ppc64le"), QStringLiteral("ppc64el"), QStringLiteral("x")));
    EXPECT_TRUE(power.archProblem.contains(QStringLiteral("ppc64el")));

    HostInfo silent = parseHostInfo(QString());
    EXPECT_TRUE(silent.agentArch.isEmpty());
    EXPECT_FALSE(silent.archProblem.isEmpty());
}

// --- Разбор вывода ----------------------------------------------------------------------

TEST(RemoteHostParse, ReadsTheFactsTheScriptReports)
{
    const HostInfo info = parseHostInfo(detectOutput(
        QStringLiteral("armv7l"), QStringLiteral("armhf"), QStringLiteral("Raspbian GNU/Linux 12 (bookworm)"), true,
        QStringLiteral("installed=spotty-uartd 1.0.0\ninstance=7777|/dev/serial/by-id/usb-FTDI-if00\n"
                       "instance=7778|/dev/ttyUSB1\nlisten_hex=1E61\nlisten_hex=0016\nlisten_hex=1E61\n")));

    EXPECT_EQ(info.system, QStringLiteral("Linux"));
    EXPECT_EQ(info.osName, QStringLiteral("Raspbian GNU/Linux 12 (bookworm)"));
    EXPECT_EQ(info.uid, 1000);
    EXPECT_TRUE(info.systemd);
    EXPECT_TRUE(info.hasSudo);
    EXPECT_EQ(info.installedVersion, QStringLiteral("1.0.0"));
    ASSERT_EQ(info.instances.size(), 2);
    EXPECT_EQ(info.instances[0].port, 7777);
    EXPECT_EQ(info.instances[0].device, QStringLiteral("/dev/serial/by-id/usb-FTDI-if00"));
    EXPECT_EQ(info.instance(7778)->device, QStringLiteral("/dev/ttyUSB1"));
    EXPECT_EQ(info.instance(9999), nullptr);
    EXPECT_TRUE(info.listening.contains(7777)); // 0x1E61
    EXPECT_TRUE(info.listening.contains(22));   // 0x0016
    EXPECT_EQ(info.listening.size(), 2);        // повтор из tcp и tcp6 не удваивается
}

TEST(RemoteHostParse, UnknownKeysAndGarbageAreSkipped)
{
    const HostInfo info = parseHostInfo(QStringLiteral(
        "uname_s=Linux\nfuture_key=whatever\nno equals sign here\n=novalue\nuid=notanumber\n"
        "instance=notaport|/dev/x\nlisten_hex=zz\nuname_m=x86_64\ndpkg_arch=amd64\n"));
    EXPECT_EQ(info.agentArch, QStringLiteral("x86_64"));
    EXPECT_EQ(info.uid, -1);
    EXPECT_TRUE(info.instances.isEmpty());
    EXPECT_TRUE(info.listening.isEmpty());
}

TEST(RemoteHostParse, SummaryNamesTheSystemArchitectureAndInitSystem)
{
    const HostInfo info = parseHostInfo(detectOutput(QStringLiteral("aarch64"), QStringLiteral("arm64"),
                                                     QStringLiteral("Debian GNU/Linux 12"), false));
    EXPECT_EQ(info.summary(), QStringLiteral("Debian GNU/Linux 12, aarch64/arm64, no systemd"));
}

TEST(RemoteHostPorts, NextFreePortSkipsAgentsAndForeignListeners)
{
    HostInfo info;
    info.instances = {{7777, QStringLiteral("/dev/ttyUSB0")}};
    info.listening = {7778, 22};
    EXPECT_EQ(nextFreePort(info, 7777), 7779);
    EXPECT_EQ(nextFreePort(info, 7780), 7780);
    EXPECT_EQ(nextFreePort(HostInfo{}, 7777), 7777);
}

TEST(RemoteHostPorts, ScanOutputKeepsEmptyColumns)
{
    const QList<UartPort> ports = parseUartScan(QStringLiteral(
        "/dev/ttyUSB0\t/dev/serial/by-id/usb-FTDI_FT232R-if00-port0\tFTDI FT232R USB UART (0403:6001)\n"
        "/dev/ttyAMA0\t\tamba-pl011\n"
        "/dev/ttyS4\t\t\n"
        "garbage without tabs\n"
        "relative/path\t\tx\n"));
    ASSERT_EQ(ports.size(), 3);
    EXPECT_EQ(ports[0].stablePath, QStringLiteral("/dev/serial/by-id/usb-FTDI_FT232R-if00-port0"));
    EXPECT_EQ(ports[0].description, QStringLiteral("FTDI FT232R USB UART (0403:6001)"));
    EXPECT_TRUE(ports[1].stablePath.isEmpty());
    EXPECT_EQ(ports[1].description, QStringLiteral("amba-pl011"));
    EXPECT_TRUE(ports[2].description.isEmpty());
}

// --- Параметры и конфиг -----------------------------------------------------------------

namespace {

InstallParams goodParams()
{
    InstallParams params;
    params.device = QStringLiteral("/dev/ttyUSB0");
    return params;
}

} // namespace

TEST(InstallParamsTest, DefaultsAreValid)
{
    EXPECT_TRUE(validateInstallParams(goodParams()).isEmpty());
}

TEST(InstallParamsTest, RejectsWhatWouldCorruptTheConfigOrTheAgent)
{
    struct Case { std::function<void(InstallParams &)> mutate; const char *mention; };
    const Case cases[] = {
        {[](InstallParams &p) { p.device.clear(); }, "Choose the serial port"},
        {[](InstallParams &p) { p.device = QStringLiteral("ttyUSB0"); }, "absolute path"},
        {[](InstallParams &p) { p.device = QStringLiteral("/dev/ttyUSB0\nport = 1"); }, "absolute path"},
        {[](InstallParams &p) { p.device = QStringLiteral("/dev/ttyUSB0 "); }, "absolute path"},
        {[](InstallParams &p) { p.baud = 123; }, "baud"},
        {[](InstallParams &p) { p.format = QStringLiteral("9N1"); }, "format"},
        {[](InstallParams &p) { p.format = QStringLiteral("8X1"); }, "format"},
        {[](InstallParams &p) { p.flow = QStringLiteral("magic"); }, "flow"},
        {[](InstallParams &p) { p.port = 0; }, "between 1 and 65535"},
        {[](InstallParams &p) { p.port = 70000; }, "between 1 and 65535"},
        {[](InstallParams &p) { p.bind = QStringLiteral("0.0.0.0\nallow = x"); }, "listen address"},
        {[](InstallParams &p) { p.bind = QStringLiteral("$(reboot)"); }, "listen address"},
        {[](InstallParams &p) { p.allow = QStringLiteral("10.0.0.1, pi-lab"); }, "pi-lab"},
    };
    for (const Case &c : cases) {
        InstallParams params = goodParams();
        c.mutate(params);
        const QString problem = validateInstallParams(params);
        EXPECT_FALSE(problem.isEmpty()) << c.mention;
        EXPECT_TRUE(problem.contains(QString::fromLatin1(c.mention), Qt::CaseInsensitive))
            << c.mention << " -> " << qPrintable(problem);
    }
}

TEST(InstallParamsTest, AcceptsTheValuesTheAgentUnderstands)
{
    InstallParams params = goodParams();
    for (int baud : supportedBauds()) {
        params.baud = baud;
        EXPECT_TRUE(validateInstallParams(params).isEmpty()) << baud;
    }
    for (const char *format : {"8N1", "8e1", "7O2", "5N1"}) {
        params.format = QString::fromLatin1(format);
        EXPECT_TRUE(validateInstallParams(params).isEmpty()) << format;
    }
    params.bind = QStringLiteral("::");
    params.allow = QStringLiteral("192.168.1.0/24, 10.0.0.5; fe80::/10");
    EXPECT_TRUE(validateInstallParams(params).isEmpty());
}

TEST(InstallParamsTest, ConfigIsRenderedFromTheTemplate)
{
    const QString templ = QStringLiteral("device = @DEVICE@\nbaud = @BAUD@\nformat = @FORMAT@\nflow = @FLOW@\n"
                                         "port = @PORT@\nbind = @BIND@\nallow = @ALLOW@\n"
                                         "# systemctl restart spotty-uartd@@PORT@\n");
    InstallParams params;
    params.device = QStringLiteral("/dev/serial/by-id/usb-X-if00");
    params.baud = 921600;
    params.format = QStringLiteral("8e1");
    params.flow = QStringLiteral("rtscts");
    params.port = 7778;
    params.bind = QStringLiteral("127.0.0.1");
    params.allow = QStringLiteral("10.0.0.0/8;192.168.1.5   fe80::/10");

    EXPECT_EQ(renderConfig(templ, params),
              QStringLiteral("device = /dev/serial/by-id/usb-X-if00\nbaud = 921600\nformat = 8E1\n"
                             "flow = rtscts\nport = 7778\nbind = 127.0.0.1\n"
                             "allow = 10.0.0.0/8, 192.168.1.5, fe80::/10\n"
                             "# systemctl restart spotty-uartd@7778\n"));
}

TEST(InstallParamsTest, TheRealTemplateLeavesNoPlaceholderBehind)
{
    QFile file(QStringLiteral(SPOTTY_AGENT_DIR "/uartd.conf.template"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QString text = renderConfig(QString::fromUtf8(file.readAll()), goodParams());
    EXPECT_FALSE(text.contains(QRegularExpression(QStringLiteral("@[A-Z]+@")))) << qPrintable(text);
    EXPECT_TRUE(text.contains(QStringLiteral("device = /dev/ttyUSB0\n")));
    EXPECT_TRUE(text.contains(QStringLiteral("port = 7777\n")));
}

// --- Кавычки для shell ------------------------------------------------------------------

TEST(ShellQuote, WrapsInSingleQuotes)
{
    EXPECT_EQ(shellQuote(QStringLiteral("abc")), QStringLiteral("'abc'"));
    EXPECT_EQ(shellQuote(QString()), QStringLiteral("''"));
    EXPECT_EQ(shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
}

#ifndef Q_OS_WIN
TEST(ShellQuote, HostileStringsComeBackUnchangedThroughARealShell)
{
    // Единственное, что стоит между строкой пользователя и shell удалённой машины.
    const QStringList hostile = {
        QStringLiteral("plain"),
        QStringLiteral("with space"),
        QStringLiteral("'; touch /tmp/pwned; '"),
        QStringLiteral("$(touch /tmp/pwned)"),
        QStringLiteral("`touch /tmp/pwned`"),
        QStringLiteral("a\"b'c\\d"),
        QStringLiteral("line1\nline2"),
        QStringLiteral("*?[~]{a,b}$HOME"),
        QStringLiteral("--option"),
    };
    for (const QString &text : hostile) {
        QProcess shell;
        shell.start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("printf %s ") + shellQuote(text)});
        ASSERT_TRUE(shell.waitForFinished(5000));
        EXPECT_EQ(QString::fromUtf8(shell.readAllStandardOutput()), text);
    }
    EXPECT_FALSE(QFile::exists(QStringLiteral("/tmp/pwned")));
}
#endif

// --- Известные хосты --------------------------------------------------------------------

TEST(KnownHostsTest, UnknownThenSameThenDifferent)
{
    KnownHosts hosts;
    EXPECT_EQ(hosts.check(QStringLiteral("pi"), 22, QStringLiteral("SHA256:a")), KnownHosts::Match::Unknown);
    ASSERT_TRUE(hosts.remember(QStringLiteral("pi"), 22, QStringLiteral("SHA256:a")));
    EXPECT_EQ(hosts.check(QStringLiteral("pi"), 22, QStringLiteral("SHA256:a")), KnownHosts::Match::Same);
    EXPECT_EQ(hosts.check(QStringLiteral("pi"), 22, QStringLiteral("SHA256:b")), KnownHosts::Match::Different);
    // Другой порт — другой сервер.
    EXPECT_EQ(hosts.check(QStringLiteral("pi"), 2222, QStringLiteral("SHA256:b")), KnownHosts::Match::Unknown);
}

TEST(KnownHostsTest, HostNamesAreCaseInsensitive)
{
    KnownHosts hosts;
    hosts.remember(QStringLiteral("PI-Lab"), 22, QStringLiteral("SHA256:a"));
    EXPECT_EQ(hosts.check(QStringLiteral("pi-lab"), 22, QStringLiteral("SHA256:a")), KnownHosts::Match::Same);
}

TEST(KnownHostsTest, SurvivesRestartAndForgetting)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("nested/known_hosts"));
    {
        KnownHosts hosts(path);
        ASSERT_TRUE(hosts.remember(QStringLiteral("10.0.0.5"), 22, QStringLiteral("SHA256:abc")));
        ASSERT_TRUE(hosts.remember(QStringLiteral("fe80::1"), 22, QStringLiteral("SHA256:v6")));
    }
    KnownHosts reloaded(path);
    EXPECT_EQ(reloaded.fingerprint(QStringLiteral("10.0.0.5"), 22), QStringLiteral("SHA256:abc"));
    EXPECT_EQ(reloaded.check(QStringLiteral("fe80::1"), 22, QStringLiteral("SHA256:v6")), KnownHosts::Match::Same);

    EXPECT_TRUE(reloaded.forget(QStringLiteral("10.0.0.5"), 22));
    EXPECT_FALSE(reloaded.forget(QStringLiteral("10.0.0.5"), 22));
    KnownHosts afterForget(path);
    EXPECT_EQ(afterForget.check(QStringLiteral("10.0.0.5"), 22, QStringLiteral("SHA256:abc")),
              KnownHosts::Match::Unknown);
    EXPECT_EQ(afterForget.check(QStringLiteral("fe80::1"), 22, QStringLiteral("SHA256:v6")),
              KnownHosts::Match::Same);
}

TEST(KnownHostsTest, UnreadableOrMissingFileIsJustEmpty)
{
    TempDir dir;
    KnownHosts hosts(dir.filePath(QStringLiteral("absent")));
    EXPECT_EQ(hosts.check(QStringLiteral("x"), 22, QStringLiteral("SHA256:a")), KnownHosts::Match::Unknown);

    QFile file(dir.filePath(QStringLiteral("junk")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("# comment\nline without tab\n\n\tno host\n");
    file.close();
    KnownHosts junk(file.fileName());
    EXPECT_EQ(junk.check(QStringLiteral("x"), 22, QStringLiteral("SHA256:a")), KnownHosts::Match::Unknown);
}

// --- Имена хостов и подпись подключения --------------------------------------------------

TEST(RemoteHostParse, ReadsTheNameTheMachineGivesItself)
{
    EXPECT_EQ(parseHostInfo(QStringLiteral("uname_s=Linux\nhostname=raspberrypi\n")).hostname, QStringLiteral("raspberrypi"));
    EXPECT_TRUE(parseHostInfo(QStringLiteral("uname_s=Linux\n")).hostname.isEmpty());
}

TEST(HostNamesTest, ShortNameKeepsTheFirstLabelOnly)
{
    EXPECT_EQ(HostNames::shortName(QStringLiteral("raspberrypi")), QStringLiteral("raspberrypi"));
    EXPECT_EQ(HostNames::shortName(QStringLiteral("raspberrypi.fritz.box")), QStringLiteral("raspberrypi"));
    EXPECT_EQ(HostNames::shortName(QStringLiteral("  justBook.local \n")), QStringLiteral("justBook"));
}

TEST(HostNamesTest, AnAddressIsNotAName)
{
    // Обратный поиск без записи возвращает сам адрес: подпись «10.0.0.5:7777» от него не улучшилась бы.
    EXPECT_TRUE(HostNames::shortName(QStringLiteral("10.0.0.5")).isEmpty());
    EXPECT_TRUE(HostNames::shortName(QStringLiteral("fe80::1")).isEmpty());
    EXPECT_TRUE(HostNames::shortName(QString()).isEmpty());

    HostNames names;
    EXPECT_FALSE(names.remember(QStringLiteral("10.0.0.5"), QStringLiteral("10.0.0.5")));
    EXPECT_FALSE(names.remember(QStringLiteral("10.0.0.5"), QString()));
    EXPECT_TRUE(names.nameFor(QStringLiteral("10.0.0.5")).isEmpty());
}

TEST(HostNamesTest, ReportsWhetherTheLabelsHaveToBeRedrawn)
{
    HostNames names;
    EXPECT_TRUE(names.remember(QStringLiteral("10.0.0.5"), QStringLiteral("raspberrypi.lan")));
    EXPECT_FALSE(names.remember(QStringLiteral("10.0.0.5"), QStringLiteral("raspberrypi"))); // то же короткое имя
    EXPECT_TRUE(names.remember(QStringLiteral("10.0.0.5"), QStringLiteral("orangepi")));
    EXPECT_EQ(names.nameFor(QStringLiteral("10.0.0.5")), QStringLiteral("orangepi"));
    EXPECT_EQ(names.nameFor(QStringLiteral(" 10.0.0.5 ")), QStringLiteral("orangepi"));
}

TEST(HostNamesTest, AHostTypedByNameNeedsNoSeparateName)
{
    HostNames names;
    EXPECT_FALSE(names.remember(QStringLiteral("pi-lab"), QStringLiteral("pi-lab.local")));
    EXPECT_TRUE(names.nameFor(QStringLiteral("pi-lab")).isEmpty());
}

TEST(HostNamesTest, SurvivesRestart)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("nested/host_names"));
    {
        HostNames names(path);
        ASSERT_TRUE(names.remember(QStringLiteral("10.0.0.5"), QStringLiteral("raspberrypi")));
    }
    HostNames reloaded(path);
    EXPECT_EQ(reloaded.nameFor(QStringLiteral("10.0.0.5")), QStringLiteral("raspberrypi"));
}

TEST(EndpointLabel, IsTheMachineNameAndThePortOnceTheNameIsKnown)
{
    HostNames names;
    QVariantMap settings{{QStringLiteral("host"), QStringLiteral("10.0.0.49")}, {QStringLiteral("port"), 7778}};

    // Имени пока нет: подпись — сам адрес (описание пусто, список покажет системное имя).
    spotty::InterfaceDescriptor unknown = describeEndpoint(settings, QStringLiteral("New"), &names);
    EXPECT_EQ(unknown.systemName, QStringLiteral("10.0.0.49:7778"));
    EXPECT_TRUE(unknown.description.isEmpty());

    names.remember(QStringLiteral("10.0.0.49"), QStringLiteral("raspberrypi.fritz.box"));
    spotty::InterfaceDescriptor named = describeEndpoint(settings, QStringLiteral("New"), &names);
    EXPECT_EQ(named.description, QStringLiteral("raspberrypi:7778"));
    EXPECT_EQ(named.systemName, QStringLiteral("10.0.0.49:7778")); // настоящий адрес остаётся виден

    // Порт по умолчанию, и без списка имён подпись не ломается.
    settings.remove(QStringLiteral("port"));
    EXPECT_EQ(describeEndpoint(settings, QStringLiteral("New"), &names).description,
              QStringLiteral("raspberrypi:7777"));
    EXPECT_TRUE(describeEndpoint(settings, QStringLiteral("New"), nullptr).description.isEmpty());

    // Пока адреса нет — подпись «New», и имя не нужно.
    EXPECT_EQ(describeEndpoint({}, QStringLiteral("New"), &names).systemName, QStringLiteral("New"));
}

TEST(EndpointLabel, IsTheMachineNameAndTheChosenSerialPort)
{
    HostNames names;
    QVariantMap settings{{QStringLiteral("host"), QStringLiteral("10.0.0.49")}, {QStringLiteral("port"), 7778},
                         {QStringLiteral("remoteUart"), QStringLiteral("/dev/ttyUSB1")}};

    // Порт машины выбран, имени ещё нет: вместо имени адрес.
    EXPECT_EQ(describeEndpoint(settings, QStringLiteral("New"), &names).description,
              QStringLiteral("10.0.0.49 ttyUSB1"));

    // С именем — «имя порт», без TCP-порта агента: порт машины и так говорит, какой это UART.
    names.remember(QStringLiteral("10.0.0.49"), QStringLiteral("raspberrypi.fritz.box"));
    EXPECT_EQ(describeEndpoint(settings, QStringLiteral("New"), &names).description,
              QStringLiteral("raspberrypi ttyUSB1"));
    EXPECT_EQ(describeEndpoint(settings, QStringLiteral("New"), nullptr).description,
              QStringLiteral("10.0.0.49 ttyUSB1"));

    // Порт ещё не выбран — прежнее «имя:порт».
    settings.remove(QStringLiteral("remoteUart"));
    EXPECT_EQ(describeEndpoint(settings, QStringLiteral("New"), &names).description,
              QStringLiteral("raspberrypi:7778"));
}

TEST(EndpointLabel, SummaryNamesTheSpeedOfThePortAndNotThePortItself)
{
    EXPECT_TRUE(uartSummary({}).isEmpty());
    EXPECT_EQ(uartSummary({{QStringLiteral("remoteUart"), QStringLiteral("/dev/ttyUSB1")},
                                      {QStringLiteral("baud"), 921600},
                                      {QStringLiteral("format"), QStringLiteral("8e1")}}),
              QStringLiteral("921600 8E1"));
}
