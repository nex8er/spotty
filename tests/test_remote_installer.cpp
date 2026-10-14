/**
 * \file test_remote_installer.cpp
 * \brief Тесты установщика агента: spotty::tcptunnel::RemoteInstaller и скриптов из каталога agent.
 *
 * «Удалённая машина» — локальный shell в песочнице (support/LocalShellSession.h), поэтому
 * выполняются настоящие скрипты и запускается настоящий агент, а проверяется порядок шагов, права,
 * пароль sudo, уборка за собой и итоговый обмен байтами через TCP и псевдотерминал.
 */
#include "RemoteInstaller.h"

#include "support/LocalShellSession.h"
#include "support/UartdSupport.h"

#include <QDir>
#include <QRegularExpression>

#include <dirent.h>

using namespace spotty::tcptunnel;
using namespace spotty::test;

namespace {

/// \brief Вывод detect.sh для Debian на x86_64: нужная машина, чтобы выбрался агент.
QString debianDetect(bool systemd, int uid = 1000, bool sudo = true, const QString &extra = {})
{
    return QStringLiteral("uname_s=Linux\nuname_m=x86_64\ndpkg_arch=amd64\nlong_bit=64\nuid=%1\n"
                          "os_name=Debian GNU/Linux 12\nsystemd=%2\nsudo=%3\n%4")
        .arg(uid)
        .arg(systemd ? QStringLiteral("yes") : QStringLiteral("no"),
             sudo ? QStringLiteral("yes") : QStringLiteral("no"), extra);
}

/// \brief Установщик, его сеанс и всё, что нужно проверять после.
struct InstallerFixture
{
    TempDir dir;
    LocalShellSession session{dir.filePath(QStringLiteral("sandbox"))};
    NativeAgentPackage package{QStringLiteral(SPOTTY_UARTD_PATH), QStringLiteral(SPOTTY_AGENT_DIR),
                               QStringLiteral(SPOTTY_AGENT_BINARIES_DIR)};
    KnownHosts known;
    QStringList progress;
    RemoteInstaller installer;

    InstallerFixture()
        : installer(session, package, known,
                    {[this](const QString &text) { progress.append(text); }, {}})
    {
    }

    ~InstallerFixture()
    {
        // Агент, запущенный скриптом без systemd, переживает тест, если тот упал на полпути.
        const QDir run(session.remotePath(QStringLiteral("/var/run")));
        for (const QString &name : run.entryList({QStringLiteral("spotty-uartd-*.pid")}, QDir::Files)) {
            QFile file(run.filePath(name));
            if (file.open(QIODevice::ReadOnly))
                ::kill(pid_t(file.readAll().trimmed().toLong()), SIGTERM);
        }
    }

    /// \brief Войти, как это делает плагин перед любой операцией: installer.connect().
    bool connect()
    {
        QString error;
        return installer.connect(access(), &error);
    }

    SshAccess access() const
    {
        SshAccess access;
        access.host = QStringLiteral("10.0.0.5");
        access.user = QStringLiteral("pi");
        access.password = QStringLiteral("secret");
        // Пользователь уже видел отпечаток и подтвердил: так выглядит повтор действия.
        access.trustFingerprint = session.fingerprint;
        return access;
    }

    /// \brief Сведения о машине, как их разобрал бы detect.sh. Режим systemd самого сеанса — отдельно
    ///        (`session.systemd`): сведения могут и расходиться с песочницей, и тест вправе это проверить.
    static HostInfo host(bool systemd = false, int uid = 1000, bool sudo = true, const QString &extra = {})
    {
        return parseHostInfo(debianDetect(systemd, uid, sudo, extra));
    }

    /// \brief Каталог промежуточных файлов, как его видно из записанных команд.
    QString stageDirectory() const
    {
        static const QRegularExpression pattern(QStringLiteral("(/[^' ]*/\\.spotty-stage\\.[A-Za-z0-9]+)"));
        for (const QString &command : session.commands) {
            const auto match = pattern.match(command);
            if (match.hasMatch())
                return match.captured(1);
        }
        return {};
    }
};

InstallParams paramsFor(const FakeUart &uart, quint16 port)
{
    InstallParams params;
    params.device = uart.path();
    params.port = port;
    params.bind = QStringLiteral("127.0.0.1");
    params.autostart = false;
    return params;
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

// --- Подключение и проверка ключа хоста -------------------------------------------------

TEST(RemoteInstallerConnect, UnknownHostKeyIsRefusedWithItsFingerprintAndNoPasswordIsSent)
{
    InstallerFixture f;
    SshAccess access = f.access();
    access.trustFingerprint.clear(); // отпечаток ещё не показывали
    QString error;

    EXPECT_FALSE(f.installer.connect(access, &error));

    EXPECT_TRUE(error.contains(QStringLiteral("SHA256:testfingerprint"))) << qPrintable(error);
    EXPECT_TRUE(error.contains(QStringLiteral("press the same button again")));
    EXPECT_EQ(f.installer.refusedFingerprint(), QStringLiteral("SHA256:testfingerprint"));
    EXPECT_TRUE(f.session.authAttempts.isEmpty()); // пароль на неизвестный хост не уходит
    EXPECT_EQ(f.known.check(access.host, 22, f.session.fingerprint), KnownHosts::Match::Unknown);
}

TEST(RemoteInstallerConnect, RepeatingAfterSeeingTheFingerprintTrustsExactlyThatKey)
{
    InstallerFixture f;
    QString error;
    ASSERT_TRUE(f.installer.connect(f.access(), &error)) << qPrintable(error); // trustFingerprint = то, что предъявлено
    EXPECT_EQ(f.known.check(QStringLiteral("10.0.0.5"), 22, f.session.fingerprint), KnownHosts::Match::Same);
    EXPECT_EQ(f.session.authAttempts.size(), 1);
    EXPECT_TRUE(f.installer.refusedFingerprint().isEmpty());

    SshAccess later = f.access();
    later.trustFingerprint.clear();
    EXPECT_TRUE(f.installer.connect(later, &error)) << qPrintable(error); // теперь известен, вопросов нет
}

TEST(RemoteInstallerConnect, ConfirmationOfAnotherKeyIsNotTrust)
{
    // Пользователь подтвердил один отпечаток, а хост предъявил другой (подмена между двумя нажатиями).
    InstallerFixture f;
    SshAccess access = f.access();
    access.trustFingerprint = QStringLiteral("SHA256:whatTheUserSaw");
    QString error;

    EXPECT_FALSE(f.installer.connect(access, &error));

    EXPECT_TRUE(error.contains(QStringLiteral("SHA256:testfingerprint")));
    EXPECT_TRUE(f.session.authAttempts.isEmpty());
    EXPECT_EQ(f.known.check(access.host, 22, QStringLiteral("SHA256:testfingerprint")), KnownHosts::Match::Unknown);
}

TEST(RemoteInstallerConnect, ChangedKeyIsRefusedEvenIfTheNewOneWasConfirmed)
{
    InstallerFixture f;
    f.known.remember(QStringLiteral("10.0.0.5"), 22, QStringLiteral("SHA256:old"));
    QString error;

    EXPECT_FALSE(f.installer.connect(f.access(), &error)); // подтверждён именно предъявленный, и всё равно нельзя

    EXPECT_TRUE(error.contains(QStringLiteral("CHANGED"))) << qPrintable(error);
    EXPECT_TRUE(f.session.authAttempts.isEmpty());
    // Старый отпечаток не перезаписан: решение «забыть» принимает человек.
    EXPECT_EQ(f.known.fingerprint(QStringLiteral("10.0.0.5"), 22), QStringLiteral("SHA256:old"));
}

TEST(RemoteInstallerConnect, KeyFromTheSystemKnownHostsIsAcceptedWithoutAskingAndNotCopied)
{
    InstallerFixture f;
    f.session.systemKey = SystemHostKey::Match;
    SshAccess access = f.access();
    access.trustFingerprint.clear();
    QString error;

    EXPECT_TRUE(f.installer.connect(access, &error)) << qPrintable(error);
    EXPECT_EQ(f.known.check(access.host, 22, f.session.fingerprint), KnownHosts::Match::Unknown);
}

TEST(RemoteInstallerConnect, KeyThatContradictsTheSystemKnownHostsIsRefused)
{
    InstallerFixture f;
    f.session.systemKey = SystemHostKey::Mismatch;
    QString error;

    EXPECT_FALSE(f.installer.connect(f.access(), &error));

    EXPECT_TRUE(error.contains(QStringLiteral("known_hosts")));
    EXPECT_TRUE(f.session.authAttempts.isEmpty());
}

TEST(RemoteInstallerConnect, TellsWhyTheConnectionOrTheSignInFailed)
{
    {
        InstallerFixture f;
        f.session.connectError = QStringLiteral("Connection refused");
        QString error;
        EXPECT_FALSE(f.installer.connect(f.access(), &error));
        EXPECT_TRUE(error.contains(QStringLiteral("10.0.0.5:22")));
        EXPECT_TRUE(error.contains(QStringLiteral("Connection refused")));
    }
    {
        InstallerFixture f;
        f.session.authError = QStringLiteral("Authentication failed");
        QString error;
        EXPECT_FALSE(f.installer.connect(f.access(), &error));
        EXPECT_TRUE(error.contains(QStringLiteral("pi")));
        EXPECT_TRUE(error.contains(QStringLiteral("Authentication failed")));
    }
    {
        InstallerFixture f;
        SshAccess access = f.access();
        QString error;
        access.host = QStringLiteral("  ");
        EXPECT_FALSE(f.installer.connect(access, &error));
        access = f.access();
        access.user.clear();
        EXPECT_FALSE(f.installer.connect(access, &error));
        EXPECT_EQ(f.session.connectCount, 0); // даже не пробовали
    }
}

// --- Определение машины -----------------------------------------------------------------

TEST(RemoteInstallerProbe, SendsTheDetectionScriptOnStandardInput)
{
    InstallerFixture f;
    f.session.cannedDetect = debianDetect(true);
    HostInfo info;
    QString error;

    ASSERT_TRUE(f.installer.probe(&info, &error)) << qPrintable(error);

    EXPECT_EQ(f.session.commands.last(), QStringLiteral("sh -s"));
    EXPECT_EQ(f.session.lastDetectInput, f.package.file(QStringLiteral("detect.sh")));
    EXPECT_EQ(info.agentArch, QStringLiteral("x86_64"));
    EXPECT_TRUE(info.systemd);
}

TEST(RemoteInstallerProbe, TheRealScriptRunsAndProducesOnlyKeyValueLines)
{
    InstallerFixture f; // скрипт выполняется на самом деле, в локальном shell
    HostInfo info;
    QString error;

    ASSERT_TRUE(f.installer.probe(&info, &error)) << qPrintable(error);

    EXPECT_FALSE(info.system.isEmpty());
    EXPECT_FALSE(info.machine.isEmpty());
    EXPECT_GE(info.uid, 0);
    EXPECT_GT(info.longBit, 0);

    // Любая лишняя строка на стандартном выводе (приветствие, предупреждение) сломала бы разбор.
    QProcess shell;
    shell.start(QStringLiteral("sh"), {QStringLiteral(SPOTTY_AGENT_DIR "/detect.sh")});
    ASSERT_TRUE(shell.waitForFinished(10000));
    for (const QString &line : QString::fromUtf8(shell.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        EXPECT_TRUE(line.contains(QLatin1Char('='))) << qPrintable(line);
}

// --- Поиск портов -----------------------------------------------------------------------

TEST(RemoteInstallerScan, RunsTheUploadedAgentAndCleansUpAfterItself)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    QList<UartPort> ports;
    QString error;

    ASSERT_TRUE(f.installer.scanUarts(f.host(), &ports, &error)) << qPrintable(error);

    // Что нашлось — зависит от машины; важно, что агент запустился и каталог убран.
    const QString stage = f.stageDirectory();
    ASSERT_FALSE(stage.isEmpty());
    EXPECT_FALSE(QDir(stage).exists());
    for (const UartPort &port : ports)
        EXPECT_TRUE(port.device.startsWith(QLatin1Char('/')));
}

TEST(RemoteInstallerScan, UnsupportedMachineIsReportedBeforeAnythingIsUploaded)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    const HostInfo mac = parseHostInfo(QStringLiteral("uname_s=Darwin\nuname_m=arm64\n"));
    QList<UartPort> ports;
    QString error;

    EXPECT_FALSE(f.installer.scanUarts(mac, &ports, &error));

    EXPECT_TRUE(error.contains(QStringLiteral("Darwin")));
    EXPECT_TRUE(f.session.commands.isEmpty());
}

TEST(RemoteInstallerScan, FallsBackToTheHomeDirectoryWhenTmpIsNotExecutable)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    int versionChecks = 0;
    f.session.intercept = [&](const QString &command, SshExecResult *result) {
        // Запуск залитого агента в /tmp отказывает, как на системе с noexec; в других местах —
        // выполняется на самом деле.
        if (command.endsWith(QStringLiteral(" --version"))) {
            ++versionChecks;
            if (command.contains(QStringLiteral("/tmp/.spotty-stage."))) {
                *result = {126, {}, "sh: /tmp/.spotty-stage.x/spotty-uartd: Permission denied\n"};
                return true;
            }
        }
        return false;
    };
    QList<UartPort> ports;
    QString error;

    ASSERT_TRUE(f.installer.scanUarts(f.host(), &ports, &error)) << qPrintable(error);

    EXPECT_EQ(versionChecks, 2);
    bool usedHome = false;
    for (const QString &command : f.session.commands)
        usedHome = usedHome || command.contains(QStringLiteral("${HOME}"));
    EXPECT_TRUE(usedHome);
}

TEST(RemoteInstallerScan, ExplainsWhenTheAgentWillNotRunAnywhere)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    f.session.intercept = [](const QString &command, SshExecResult *result) {
        if (command.endsWith(QStringLiteral(" --version"))) {
            *result = {126, {}, "Exec format error\n"};
            return true;
        }
        return false;
    };
    QList<UartPort> ports;
    QString error;

    EXPECT_FALSE(f.installer.scanUarts(f.host(), &ports, &error));

    EXPECT_TRUE(error.contains(QStringLiteral("Exec format error"))) << qPrintable(error);
    EXPECT_TRUE(error.contains(QStringLiteral("Debian GNU/Linux 12"))); // что за машина, по мнению Spotty
}

// --- Установка без systemd: настоящий агент ---------------------------------------------

TEST(RemoteInstallerInstall, InstallsStartsAndRunsTheAgentThroughSudo)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    FakeUart uart;
    const quint16 port = freePort();
    HostInfo info = f.host(/*systemd=*/false, /*uid=*/1000);
    QString error;

    ASSERT_TRUE(f.installer.install(info, paramsFor(uart, port), &error)) << qPrintable(error);

    // Файлы на месте и с нужными правами.
    const QFileInfo binary(f.session.remotePath(QStringLiteral("/usr/local/bin/spotty-uartd")));
    ASSERT_TRUE(binary.exists());
    EXPECT_TRUE(binary.isExecutable());
    const QString conf = readText(f.session.remotePath(QStringLiteral("/etc/spotty-uartd/%1.conf").arg(port)));
    EXPECT_TRUE(conf.contains(QStringLiteral("device = %1\n").arg(uart.path()))) << qPrintable(conf);
    EXPECT_TRUE(conf.contains(QStringLiteral("port = %1\n").arg(port)));
    EXPECT_TRUE(conf.contains(QStringLiteral("bind = 127.0.0.1\n")));

    // Привилегированная часть пошла через sudo -S, а пароль — только в канал ввода.
    bool sudoCommand = false;
    for (const QString &command : f.session.commands) {
        EXPECT_FALSE(command.contains(QStringLiteral("secret"))) << qPrintable(command);
        sudoCommand = sudoCommand || (command.startsWith(QStringLiteral("sudo -S -p '' sh '")) && command.contains(QStringLiteral("install.sh' %1 0").arg(port)));
    }
    EXPECT_TRUE(sudoCommand);

    // Промежуточный каталог убран.
    EXPECT_FALSE(QDir(f.stageDirectory()).exists());

    // Ход работы виден пользователю.
    EXPECT_TRUE(f.progress.contains(QStringLiteral("Uploading the files...")));
    EXPECT_TRUE(f.progress.contains(QStringLiteral("starting the agent")));

    // И главное: агент запущен и перекладывает байты.
    Client client(port);
    ASSERT_TRUE(client.connected);
    // Байты из порта, пришедшие раньше, чем агент принял клиента, пропали бы: клиентов у него ещё нет.
    const QString agentLog = f.session.remotePath(QStringLiteral("/var/log/spotty-uartd-%1.log").arg(port));
    ASSERT_TRUE(waitFor(
        [&] {
            QFile log(agentLog);
            return log.open(QIODevice::ReadOnly) && log.readAll().contains("connected (1 total)");
        },
        10000));
    uart.emitBytes("from-uart");
    ASSERT_TRUE(client.waitForBytes(9));
    EXPECT_EQ(client.received, QByteArray("from-uart"));
    client.socket.write("to-uart");
    client.socket.flush();
    EXPECT_EQ(uart.receiveBytes(7), QByteArray("to-uart"));
}

TEST(RemoteInstallerInstall, ReinstallAppliesTheNewSettingsAndRestartsTheAgent)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    FakeUart uart;
    const quint16 port = freePort();
    HostInfo info = f.host();
    QString error;
    InstallParams params = paramsFor(uart, port);
    ASSERT_TRUE(f.installer.install(info, params, &error)) << qPrintable(error);
    const QString pidFile = f.session.remotePath(QStringLiteral("/var/run/spotty-uartd-%1.pid").arg(port));
    const QString firstPid = readText(pidFile).trimmed();
    ASSERT_FALSE(firstPid.isEmpty());

    // Агент уже стоит: повторный запуск — перенастройка того же порта и того же UART.
    info = parseHostInfo(debianDetect(false, 1000, true,
                                      QStringLiteral("instance=%1|%2\nlisten_hex=%3\n")
                                          .arg(port)
                                          .arg(uart.path())
                                          .arg(port, 0, 16)));
    params.baud = 9600;
    params.format = QStringLiteral("7E1");
    uart.releaseExclusive(); // первый агент ещё держит порт, а у настоящего он освободился бы с его выходом
    ASSERT_TRUE(f.installer.install(info, params, &error)) << qPrintable(error);

    const QString secondPid = readText(pidFile).trimmed();
    EXPECT_NE(firstPid, secondPid); // перезапущен
    EXPECT_TRUE(readText(f.session.remotePath(QStringLiteral("/etc/spotty-uartd/%1.conf").arg(port)))
                    .contains(QStringLiteral("baud = 9600\n")));

    Client client(port);
    ASSERT_TRUE(client.connected);
    struct termios tio;
    ASSERT_TRUE(waitFor([&] { return uart.termios(&tio) && cfgetispeed(&tio) == speed_t(B9600); }));
}

TEST(RemoteInstallerInstall, UninstallStopsTheAgentAndRemovesEverythingTheLastInstanceOwned)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    FakeUart uart;
    const quint16 port = freePort();
    QString error;
    ASSERT_TRUE(f.installer.install(f.host(), paramsFor(uart, port), &error)) << qPrintable(error);
    {
        Client alive(port);
        ASSERT_TRUE(alive.connected);
    }

    const HostInfo installed = parseHostInfo(
        debianDetect(false, 1000, true, QStringLiteral("instance=%1|%2\n").arg(port).arg(uart.path())));
    ASSERT_TRUE(f.installer.uninstall(installed, port, &error)) << qPrintable(error);

    EXPECT_FALSE(QFile::exists(f.session.remotePath(QStringLiteral("/etc/spotty-uartd/%1.conf").arg(port))));
    EXPECT_FALSE(QFile::exists(f.session.remotePath(QStringLiteral("/usr/local/bin/spotty-uartd"))));
    EXPECT_FALSE(QFile::exists(f.session.remotePath(QStringLiteral("/var/run/spotty-uartd-%1.pid").arg(port))));
    EXPECT_FALSE(QDir(f.stageDirectory()).exists());

    // Агент остановлен: порт больше никого не слушает.
    QTcpSocket probe;
    probe.connectToHost(QHostAddress::LocalHost, port);
    EXPECT_FALSE(probe.waitForConnected(1500));
}

TEST(RemoteInstallerInstall, SecondAgentOnTheSameMachineLeavesTheFirstAlone)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    FakeUart first;
    FakeUart second;
    const quint16 port1 = freePort();
    quint16 port2 = freePort();
    while (port2 == port1)
        port2 = freePort();
    QString error;
    ASSERT_TRUE(f.installer.install(f.host(), paramsFor(first, port1), &error)) << qPrintable(error);
    ASSERT_TRUE(f.installer.install(f.host(), paramsFor(second, port2), &error)) << qPrintable(error);

    const HostInfo both = parseHostInfo(debianDetect(
        false, 1000, true,
        QStringLiteral("instance=%1|%2\ninstance=%3|%4\n").arg(port1).arg(first.path()).arg(port2).arg(second.path())));
    ASSERT_TRUE(f.installer.uninstall(both, port1, &error)) << qPrintable(error);

    // Бинарник нужен второму, поэтому остался; первый больше не слушает, второй — да.
    EXPECT_TRUE(QFile::exists(f.session.remotePath(QStringLiteral("/usr/local/bin/spotty-uartd"))));
    EXPECT_FALSE(QFile::exists(f.session.remotePath(QStringLiteral("/etc/spotty-uartd/%1.conf").arg(port1))));
    EXPECT_TRUE(QFile::exists(f.session.remotePath(QStringLiteral("/etc/spotty-uartd/%1.conf").arg(port2))));
    Client stillThere(port2);
    EXPECT_TRUE(stillThere.connected);
}

// --- Установка с systemd (заменитель systemctl) -----------------------------------------

TEST(RemoteInstallerInstall, WithSystemdItEnablesAndRestartsTheTemplateUnit)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    f.session.systemd = true;
    FakeUart uart;
    InstallParams params = paramsFor(uart, 7791);
    params.autostart = true;
    QString error;

    ASSERT_TRUE(f.installer.install(f.host(/*systemd=*/true), params, &error)) << qPrintable(error);

    EXPECT_TRUE(QFile::exists(f.session.remotePath(QStringLiteral("/etc/systemd/system/spotty-uartd@.service"))));
    const QStringList calls = f.session.systemctlCalls();
    EXPECT_TRUE(calls.contains(QStringLiteral("daemon-reload"))) << qPrintable(calls.join('|'));
    EXPECT_TRUE(calls.contains(QStringLiteral("enable spotty-uartd@7791.service")));
    EXPECT_TRUE(calls.contains(QStringLiteral("restart spotty-uartd@7791.service")));
    EXPECT_LT(calls.indexOf(QStringLiteral("enable spotty-uartd@7791.service")),
              calls.indexOf(QStringLiteral("restart spotty-uartd@7791.service")));
}

TEST(RemoteInstallerInstall, WithoutAutostartAnEarlierEnableIsUndone)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    f.session.systemd = true;
    FakeUart uart;
    InstallParams params = paramsFor(uart, 7792);
    params.autostart = false;
    QString error;

    ASSERT_TRUE(f.installer.install(f.host(true), params, &error)) << qPrintable(error);

    const QStringList calls = f.session.systemctlCalls();
    EXPECT_TRUE(calls.contains(QStringLiteral("disable spotty-uartd@7792.service")));
    EXPECT_FALSE(calls.contains(QStringLiteral("enable spotty-uartd@7792.service")));
    EXPECT_TRUE(calls.contains(QStringLiteral("restart spotty-uartd@7792.service")));
}

TEST(RemoteInstallerInstall, UninstallWithSystemdDisablesTheInstanceAndDropsTheUnitWithTheLastOne)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    f.session.systemd = true;
    FakeUart uart;
    QString error;
    InstallParams params = paramsFor(uart, 7793);
    params.autostart = true;
    ASSERT_TRUE(f.installer.install(f.host(true), params, &error)) << qPrintable(error);

    const HostInfo installed = parseHostInfo(
        debianDetect(true, 1000, true, QStringLiteral("instance=7793|%1\n").arg(uart.path())));
    ASSERT_TRUE(f.installer.uninstall(installed, 7793, &error)) << qPrintable(error);

    EXPECT_TRUE(f.session.systemctlCalls().contains(QStringLiteral("disable --now spotty-uartd@7793.service")));
    EXPECT_FALSE(QFile::exists(f.session.remotePath(QStringLiteral("/etc/systemd/system/spotty-uartd@.service"))));
    EXPECT_FALSE(QFile::exists(f.session.remotePath(QStringLiteral("/usr/local/bin/spotty-uartd"))));
}

// --- Права и отказы ---------------------------------------------------------------------

TEST(RemoteInstallerInstall, RootNeedsNoSudo)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    FakeUart uart;
    const quint16 port = freePort();
    QString error;

    ASSERT_TRUE(f.installer.install(f.host(false, /*uid=*/0, /*sudo=*/false), paramsFor(uart, port), &error))
        << qPrintable(error);

    for (const QString &command : f.session.commands)
        EXPECT_FALSE(command.contains(QStringLiteral("sudo"))) << qPrintable(command);
}

TEST(RemoteInstallerInstall, WrongSudoPasswordIsExplainedAndNothingIsLeftBehind)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    f.session.sudoPassword = QStringLiteral("the-real-one");
    FakeUart uart;
    QString error;

    EXPECT_FALSE(f.installer.install(f.host(), paramsFor(uart, freePort()), &error));

    EXPECT_TRUE(error.contains(QStringLiteral("sudo did not accept the password"))) << qPrintable(error);
    EXPECT_TRUE(error.contains(QStringLiteral("pi"))); // чьего пароля ждут
    EXPECT_FALSE(QFile::exists(f.session.remotePath(QStringLiteral("/usr/local/bin/spotty-uartd"))));
    EXPECT_FALSE(QDir(f.stageDirectory()).exists());
}

TEST(RemoteInstallerInstall, NotRootAndNoSudoIsExplained)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    FakeUart uart;
    QString error;

    EXPECT_FALSE(f.installer.install(f.host(false, 1000, /*sudo=*/false), paramsFor(uart, freePort()), &error));

    EXPECT_TRUE(error.contains(QStringLiteral("sudo is not installed"))) << qPrintable(error);
    EXPECT_FALSE(QDir(f.stageDirectory()).exists());
}

TEST(RemoteInstallerInstall, AutostartWithoutSystemdIsRefusedBeforeAnythingHappens)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    FakeUart uart;
    InstallParams params = paramsFor(uart, freePort());
    params.autostart = true;
    QString error;

    EXPECT_FALSE(f.installer.install(f.host(/*systemd=*/false), params, &error));

    EXPECT_TRUE(error.contains(QStringLiteral("systemd")));
    EXPECT_TRUE(f.session.commands.isEmpty());
}

TEST(RemoteInstallerInstall, InvalidParametersNeverReachTheMachine)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    InstallParams params;
    params.device = QStringLiteral("/dev/ttyUSB0\nport = 1");
    QString error;

    EXPECT_FALSE(f.installer.install(f.host(), params, &error));

    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(f.session.commands.isEmpty());
}

TEST(RemoteInstallerInstall, PortUsedByAnotherProgramSuggestsTheNextFreeOne)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    FakeUart uart;
    const HostInfo busy = f.host(false, 1000, true, QStringLiteral("listen_hex=%1\n").arg(7777, 0, 16));
    QString error;

    EXPECT_FALSE(f.installer.install(busy, paramsFor(uart, 7777), &error));

    EXPECT_TRUE(error.contains(QStringLiteral("another program"))) << qPrintable(error);
    EXPECT_TRUE(error.contains(QStringLiteral("7778")));
    EXPECT_TRUE(f.session.commands.isEmpty());
}

TEST(RemoteInstallerInstall, PortOfAnotherUartIsNotTakenOverSilently)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    FakeUart uart;
    const quint16 port = freePort();
    const HostInfo taken = parseHostInfo(debianDetect(
        false, 1000, true,
        QStringLiteral("instance=%1|/dev/serial/by-id/usb-SOMEONE_ELSE-if00\nlisten_hex=%2\n").arg(port).arg(port, 0, 16)));
    QString error;

    EXPECT_FALSE(f.installer.install(taken, paramsFor(uart, port), &error));

    EXPECT_TRUE(error.contains(QStringLiteral("usb-SOMEONE_ELSE"))) << qPrintable(error);
    EXPECT_TRUE(error.contains(QStringLiteral("remove that agent first")));
    EXPECT_FALSE(QFile::exists(f.session.remotePath(QStringLiteral("/etc/spotty-uartd/%1.conf").arg(port))));
}

TEST(RemoteInstallerInstall, UninstallOfAMissingInstanceIsAnError)
{
    InstallerFixture f;
    ASSERT_TRUE(f.connect());
    QString error;
    EXPECT_FALSE(f.installer.uninstall(f.host(), 7777, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("no agent on port 7777")));
    EXPECT_TRUE(f.session.commands.isEmpty());
}

// --- Сами скрипты -----------------------------------------------------------------------

namespace {

int runScript(const QString &name, const QStringList &arguments, QString *err = nullptr)
{
    QProcess process;
    process.start(QStringLiteral("sh"), QStringList{QStringLiteral(SPOTTY_AGENT_DIR "/") + name} + arguments);
    process.waitForFinished(10000);
    if (err)
        *err = QString::fromUtf8(process.readAllStandardError());
    return process.exitCode();
}

} // namespace

TEST(AgentScripts, PortArgumentIsNeverTreatedAsShell)
{
    // Порт попадает в имена файлов и сервисов; всё, что не число, отвергается до единого действия.
    for (const QString &bad : {QStringLiteral("7777; touch /tmp/pwned"), QStringLiteral("$(touch /tmp/pwned)"),
                               QStringLiteral(""), QStringLiteral("-1"), QStringLiteral("0"),
                               QStringLiteral("70000"), QStringLiteral("77 77")}) {
        QString err;
        EXPECT_NE(runScript(QStringLiteral("install.sh"), {bad, QStringLiteral("0")}, &err), 0) << qPrintable(bad);
        EXPECT_TRUE(err.contains(QStringLiteral("ERROR"))) << qPrintable(bad) << ": " << qPrintable(err);
        EXPECT_NE(runScript(QStringLiteral("uninstall.sh"), {bad}), 0) << qPrintable(bad);
    }
    EXPECT_FALSE(QFile::exists(QStringLiteral("/tmp/pwned")));
}

TEST(AgentScripts, AutostartFlagMustBeZeroOrOne)
{
    EXPECT_NE(runScript(QStringLiteral("install.sh"), {QStringLiteral("7777"), QStringLiteral("2")}), 0);
    EXPECT_NE(runScript(QStringLiteral("install.sh"), {QStringLiteral("7777")}), 0);
}

TEST(AgentScripts, InstallRefusesToRunWithoutItsFiles)
{
    // Скрипт запускается из каталога исходников, где нет ни бинарника, ни конфига, и должен
    // отказаться, а не записать пустые файлы в /usr/local/bin.
    QString err;
    EXPECT_NE(runScript(QStringLiteral("install.sh"), {QStringLiteral("7777"), QStringLiteral("0")}, &err), 0);
    EXPECT_TRUE(err.contains(QStringLiteral("is missing"))) << qPrintable(err);
}

// --- Ветки скриптов, которые на этой системе не выполняются сами --------------------------
//
// Разбор вывода Linux (os-release, /proc, каталог systemd) нельзя проверить запуском на macOS, а
// ошибка там сломала бы определение машины или перезапуск агента именно у пользователя. Поэтому
// скрипты принимают корень и каталог proc, а тесты кладут туда образцы настоящих файлов.

namespace {

void writeText(const QString &path, const QByteArray &content, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly)) << qPrintable(path);
    file.write(content);
    file.close();
    if (executable)
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
}

QString runWithEnvironment(const QString &script, const QStringList &arguments,
                           const QHash<QString, QString> &variables, int *exitCode = nullptr)
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it)
        env.insert(it.key(), it.value());
    process.setProcessEnvironment(env);
    process.start(QStringLiteral("sh"), QStringList{QStringLiteral(SPOTTY_AGENT_DIR "/") + script} + arguments);
    process.waitForFinished(10000);
    if (exitCode)
        *exitCode = process.exitCode();
    return QString::fromUtf8(process.readAllStandardOutput());
}

} // namespace

TEST(AgentScripts, DetectReadsTheFactsOfARealLinuxFromTheMachinesFiles)
{
    TempDir dir;
    const QString root = dir.filePath(QStringLiteral("root"));
    writeText(root + QStringLiteral("/etc/os-release"),
              "PRETTY_NAME=\"Raspbian GNU/Linux 12 (bookworm)\"\nNAME=\"Raspbian GNU/Linux\"\nVERSION_ID=\"12\"\n"
              "ID=raspbian\nID_LIKE=debian\n");
    QDir().mkpath(root + QStringLiteral("/run/systemd/system"));
    writeText(root + QStringLiteral("/etc/spotty-uartd/7777.conf"),
              "# written by Spotty\ndevice = /dev/serial/by-id/usb-FTDI_FT232R-if00-port0\nbaud = 115200\n");
    writeText(root + QStringLiteral("/etc/spotty-uartd/7778.conf"), "device=/dev/ttyUSB1\nport=7778\n");
    writeText(root + QStringLiteral("/usr/local/bin/spotty-uartd"), "#!/bin/sh\necho \"spotty-uartd 1.0.0\"\n", true);

    // Образец /proc/net/tcp с настоящей машины: слушают 7777 (1E61), 631 (0277), установленное
    // соединение (состояние 01) в список не входит.
    writeText(dir.filePath(QStringLiteral("proc/net/tcp")),
              "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
              "   0: 00000000:1E61 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 12345 1 0000000000000000 100 0 0 10 0\n"
              "   1: 0100007F:0277 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 2345 1 0000000000000000 100 0 0 10 0\n"
              "   2: 0F00000A:1E62 0100000A:C3E4 01 00000000:00000000 00:00000000 00000000  1000        0 6789 1 0000000000000000 20 4 30 10 -1\n");
    writeText(dir.filePath(QStringLiteral("proc/net/tcp6")),
              "  sl  local_address                         remote_address                        st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
              "   0: 00000000000000000000000000000000:0016 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 1111 1 0000000000000000 100 0 0 10 0\n");

    const QString output = runWithEnvironment(QStringLiteral("detect.sh"), {},
                                              {{QStringLiteral("SPOTTY_ROOT"), root},
                                               {QStringLiteral("SPOTTY_PROC"), dir.filePath(QStringLiteral("proc"))}});
    const HostInfo info = parseHostInfo(output);

    EXPECT_EQ(info.osName, QStringLiteral("Raspbian GNU/Linux 12 (bookworm)"));
    EXPECT_TRUE(info.systemd);
    EXPECT_EQ(info.installedVersion, QStringLiteral("1.0.0"));
    ASSERT_EQ(info.instances.size(), 2) << qPrintable(output);
    EXPECT_EQ(info.instance(7777)->device, QStringLiteral("/dev/serial/by-id/usb-FTDI_FT232R-if00-port0"));
    EXPECT_EQ(info.instance(7778)->device, QStringLiteral("/dev/ttyUSB1"));
    EXPECT_TRUE(info.listening.contains(7777));
    EXPECT_TRUE(info.listening.contains(631));
    EXPECT_TRUE(info.listening.contains(22)); // из tcp6
    EXPECT_FALSE(info.listening.contains(0x1E62)); // установленное соединение — не слушающий порт
}

TEST(AgentScripts, DetectOnAMachineWithoutSystemdAndAgentSaysSo)
{
    TempDir dir;
    const QString root = dir.filePath(QStringLiteral("root"));
    QDir().mkpath(root);
    const HostInfo info = parseHostInfo(runWithEnvironment(QStringLiteral("detect.sh"), {},
                                                          {{QStringLiteral("SPOTTY_ROOT"), root},
                                                           {QStringLiteral("SPOTTY_PROC"), dir.filePath(QStringLiteral("proc"))}}));
    EXPECT_FALSE(info.systemd);
    EXPECT_TRUE(info.installedVersion.isEmpty());
    EXPECT_TRUE(info.instances.isEmpty());
    EXPECT_TRUE(info.listening.isEmpty());
}

TEST(AgentScripts, RemovalStopsTheProcessByTheProcCmdlineAndOnlyIfItIsOurAgent)
{
    // /proc/<pid>/cmdline — как у настоящего Linux: аргументы через NUL.
    auto runCase = [](const QByteArray &cmdline, bool expectKilled) {
        TempDir dir;
        const QString root = dir.filePath(QStringLiteral("root"));
        const QString proc = dir.filePath(QStringLiteral("proc"));
        QProcess victim;
        victim.start(QStringLiteral("sleep"), {QStringLiteral("60")});
        ASSERT_TRUE(victim.waitForStarted(3000));
        const qint64 pid = victim.processId();

        writeText(proc + QStringLiteral("/%1/cmdline").arg(pid), cmdline);
        writeText(root + QStringLiteral("/var/run/spotty-uartd-7777.pid"), QByteArray::number(pid) + "\n");

        int code = -1;
        runWithEnvironment(QStringLiteral("uninstall.sh"), {QStringLiteral("7777")},
                           {{QStringLiteral("SPOTTY_ROOT"), root},
                            {QStringLiteral("SPOTTY_PROC"), proc},
                            {QStringLiteral("SPOTTY_SYSTEMD"), QStringLiteral("no")}},
                           &code);
        EXPECT_EQ(code, 0);

        if (expectKilled) {
            EXPECT_TRUE(victim.waitForFinished(3000)) << "агент из pid-файла не остановлен";
        } else {
            // Чужой процесс под тем же номером убивать нельзя: pid из файла мог достаться кому угодно.
            EXPECT_FALSE(victim.waitForFinished(500));
            victim.kill();
            victim.waitForFinished(2000);
        }
        // Pid-файл убирается в любом случае: он устарел.
        EXPECT_FALSE(QFile::exists(root + QStringLiteral("/var/run/spotty-uartd-7777.pid")));
    };

    runCase(QByteArray("/usr/local/bin/spotty-uartd\0--config\0/etc/spotty-uartd/7777.conf\0", 63), true);
    runCase(QByteArray("/bin/sleep\0" "60\0", 15), false);
}
