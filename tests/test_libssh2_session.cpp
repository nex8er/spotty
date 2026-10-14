/**
 * \file test_libssh2_session.cpp
 * \brief Проверка настоящего ssh-транспорта (spotty::tcptunnel::Libssh2Session) на настоящем sshd.
 *
 * Сервер поднимается без прав root на свободном порту петли, со своими ключами и входом по
 * ключу — пароли без PAM у непривилегированного sshd проверить нечем. Если sshd или ssh-keygen
 * на машине нет, тесты пропускаются: они проверяют транспорт, а не наличие OpenSSH.
 */
#include "Libssh2Session.h"

#include "support/TestSupport.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

#include <gtest/gtest.h>

#include <pwd.h>
#include <unistd.h>

#include <memory>

using namespace spotty::tcptunnel;
using spotty::test::TempDir;
using spotty::test::waitFor;

namespace {

QString findTool(const QString &name)
{
    return QStandardPaths::findExecutable(
        name, {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/sbin"),
               QStringLiteral("/usr/local/bin"), QStringLiteral("/opt/homebrew/sbin"),
               QStringLiteral("/opt/homebrew/bin")});
}

/**
 * \brief Непривилегированный sshd со своими ключами; пользователь — тот, кто запустил тесты.
 *
 * В конфиге только опции, которые знает любой OpenSSH за последние годы: неизвестная (например,
 * `KbdInteractiveAuthentication`, которой нет до 8.7) не дала бы серверу запуститься, и тесты на
 * старой системе молча пропускались бы.
 */
class TestSshd
{
public:
    TestSshd()
    {
        const QString sshd = findTool(QStringLiteral("sshd"));
        const QString keygen = findTool(QStringLiteral("ssh-keygen"));
        if (sshd.isEmpty() || keygen.isEmpty()) {
            m_skip = QStringLiteral("sshd or ssh-keygen is not installed");
            return;
        }

        // Под root (контейнер CI) sshd разделяет привилегии и требует каталог /run/sshd, который
        // обычно создаёт служба. Пакет в контейнере её не запускает.
        if (::geteuid() == 0)
            QDir().mkpath(QStringLiteral("/run/sshd"));

        // Порт: прослушать и отпустить. Небольшая гонка на тестовой машине терпима.
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0)) {
            m_skip = QStringLiteral("no free port");
            return;
        }
        m_port = probe.serverPort();
        probe.close();

        if (!makeKey(keygen, QStringLiteral("host"), {}) || !makeKey(keygen, QStringLiteral("client"), {})
            || !makeKey(keygen, QStringLiteral("locked"), QStringLiteral("correct horse"))) {
            m_skip = QStringLiteral("ssh-keygen failed");
            return;
        }
        QFile authorized(path(QStringLiteral("authorized_keys")));
        if (!authorized.open(QIODevice::WriteOnly))
            return;
        // Подходят два ключа: без пароля и с паролем. Третий («stranger») в файл не входит.
        authorized.write(readFile(path(QStringLiteral("client.pub"))));
        authorized.write(readFile(path(QStringLiteral("locked.pub"))));
        authorized.close();
        if (!makeKey(keygen, QStringLiteral("stranger"), {}))
            return;

        QFile config(path(QStringLiteral("sshd_config")));
        if (!config.open(QIODevice::WriteOnly))
            return;
        config.write(QStringLiteral("Port %1\nListenAddress 127.0.0.1\nHostKey %2\nPidFile %3\n"
                                    "AuthorizedKeysFile %4\nPasswordAuthentication no\n"
                                    "PubkeyAuthentication yes\nUsePAM no\n"
                                    "StrictModes no\nLogLevel ERROR\n")
                         .arg(m_port)
                         .arg(path(QStringLiteral("host")), path(QStringLiteral("sshd.pid")),
                              path(QStringLiteral("authorized_keys")))
                         .toUtf8());
        config.close();

        m_process.setProgram(sshd);
        m_process.setArguments({QStringLiteral("-D"), QStringLiteral("-e"), QStringLiteral("-f"),
                                path(QStringLiteral("sshd_config"))});
        m_process.setProcessChannelMode(QProcess::MergedChannels);
        m_process.start();
        if (!m_process.waitForStarted(3000)) {
            m_skip = QStringLiteral("sshd did not start");
            return;
        }

        // Готов, когда порт принимает соединения.
        const bool up = waitFor(
            [&] {
                QTcpSocket socket;
                socket.connectToHost(QHostAddress::LocalHost, m_port);
                return socket.waitForConnected(200);
            },
            4000);
        if (!up)
            m_skip = QStringLiteral("sshd does not listen: ") + QString::fromUtf8(m_process.readAll());
    }

    ~TestSshd()
    {
        m_process.terminate();
        if (!m_process.waitForFinished(2000))
            m_process.kill();
    }

    QString skipReason() const { return m_skip; }
    int port() const { return m_port; }
    QString path(const QString &name) const { return m_dir.filePath(name); }
    /// \brief Имя владельца процесса. Не из $USER: в контейнере CI его нет, и тесты идут от root.
    QString user() const
    {
        const struct passwd *entry = ::getpwuid(::geteuid());
        return entry ? QString::fromLocal8Bit(entry->pw_name) : QStringLiteral("nobody");
    }
    QString dir() const { return m_dir.path(); }

    /// \brief Отпечаток ключа хоста так, как его печатает ssh-keygen -l.
    QString hostFingerprint() const
    {
        QProcess process;
        process.start(findTool(QStringLiteral("ssh-keygen")),
                      {QStringLiteral("-l"), QStringLiteral("-f"), path(QStringLiteral("host.pub"))});
        process.waitForFinished(5000);
        return QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char(' ')).value(1);
    }

    SshAccess access(const QString &key = QStringLiteral("client"), const QString &passphrase = {}) const
    {
        SshAccess access;
        access.host = QStringLiteral("127.0.0.1");
        access.port = m_port;
        access.user = user();
        access.keyFile = path(key);
        access.keyPassphrase = passphrase;
        access.timeoutMs = 10000;
        return access;
    }

private:
    static QByteArray readFile(const QString &file)
    {
        QFile f(file);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    bool makeKey(const QString &keygen, const QString &name, const QString &passphrase)
    {
        QProcess process;
        process.start(keygen, {QStringLiteral("-q"), QStringLiteral("-t"), QStringLiteral("ed25519"),
                               QStringLiteral("-N"), passphrase, QStringLiteral("-f"), path(name)});
        return process.waitForFinished(10000) && process.exitCode() == 0;
    }

    TempDir m_dir;
    QProcess m_process;
    QString m_skip;
    int m_port = 0;
};

/// \brief Открытый сеанс, уже вошедший по ключу без пароля.
struct Connected
{
    explicit Connected(const TestSshd &sshd)
    {
        QString error;
        ok = session.connectToHost(QStringLiteral("127.0.0.1"), sshd.port(), 10000, &error)
             && session.authenticate(sshd.access(), &error);
        if (!ok)
            ADD_FAILURE() << qPrintable(error);
    }

    Libssh2Session session;
    bool ok = false;
};

} // namespace

#define REQUIRE_SSHD(name)                                          \
    TestSshd name;                                                  \
    if (!name.skipReason().isEmpty())                               \
        GTEST_SKIP() << qPrintable(name.skipReason())

TEST(Libssh2Session, FingerprintMatchesTheOneSshKeygenPrints)
{
    REQUIRE_SSHD(sshd);
    Libssh2Session session;
    QString error;
    ASSERT_TRUE(session.connectToHost(QStringLiteral("127.0.0.1"), sshd.port(), 10000, &error)) << qPrintable(error);

    // Сверка с тем, что человек увидит на самой машине и сравнит с отпечатком в окне Spotty.
    EXPECT_EQ(session.hostKeyFingerprint(), sshd.hostFingerprint());
    EXPECT_TRUE(session.hostKeyFingerprint().startsWith(QStringLiteral("SHA256:")));
}

TEST(Libssh2Session, SignsInWithAKeyAndRunsCommands)
{
    REQUIRE_SSHD(sshd);
    Connected c(sshd);
    ASSERT_TRUE(c.ok);
    SshExecResult result;
    QString error;

    ASSERT_TRUE(c.session.exec(QStringLiteral("echo hello; echo oops 1>&2"), {}, 10000, &result, &error))
        << qPrintable(error);

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.out, QByteArray("hello\n"));
    EXPECT_EQ(result.err, QByteArray("oops\n"));
}

TEST(Libssh2Session, ReturnsTheExitCodeOfTheCommand)
{
    REQUIRE_SSHD(sshd);
    Connected c(sshd);
    ASSERT_TRUE(c.ok);
    SshExecResult result;
    QString error;

    ASSERT_TRUE(c.session.exec(QStringLiteral("exit 7"), {}, 10000, &result, &error)) << qPrintable(error);
    EXPECT_EQ(result.exitCode, 7);

    // Несколько команд подряд в одном сеансе: каждая — свой канал.
    ASSERT_TRUE(c.session.exec(QStringLiteral("echo second"), {}, 10000, &result, &error));
    EXPECT_EQ(result.out, QByteArray("second\n"));
    EXPECT_EQ(result.exitCode, 0);
}

TEST(Libssh2Session, FeedsStandardInputAndUploadsABinaryFileIntact)
{
    REQUIRE_SSHD(sshd);
    Connected c(sshd);
    ASSERT_TRUE(c.ok);

    // Всё 0..255 несколько раз: нули и переводы строк не должны ни теряться, ни меняться.
    QByteArray payload;
    for (int i = 0; i < 400; ++i)
        for (int b = 0; b < 256; ++b)
            payload.append(char(b));

    const QString target = QDir(sshd.dir()).filePath(QStringLiteral("uploaded.bin"));
    SshExecResult result;
    QString error;
    ASSERT_TRUE(c.session.exec(QStringLiteral("cat > '%1' && chmod 755 '%1'").arg(target), payload, 10000, &result, &error))
        << qPrintable(error);

    EXPECT_EQ(result.exitCode, 0);
    QFile file(target);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), payload);
    EXPECT_TRUE(file.permissions() & QFileDevice::ExeOwner);
}

TEST(Libssh2Session, BigInputAndBigOutputAtOnceDoNotDeadlock)
{
    REQUIRE_SSHD(sshd);
    Connected c(sshd);
    ASSERT_TRUE(c.ok);

    // Мегабайт туда и мегабайт обратно через cat: пока мы заливаем вход, cat заполняет свой
    // выход, и обмен встаёт, если запись не чередуется с чтением.
    QByteArray payload(1024 * 1024, '\0');
    for (qsizetype i = 0; i < payload.size(); ++i)
        payload[i] = char(QRandomGenerator::global()->bounded(256));
    SshExecResult result;
    QString error;

    ASSERT_TRUE(c.session.exec(QStringLiteral("cat"), payload, 30000, &result, &error)) << qPrintable(error);

    EXPECT_EQ(result.out.size(), payload.size());
    EXPECT_EQ(result.out, payload);
}

TEST(Libssh2Session, SlowCommandRunsOutOfTimeInsteadOfHangingTheCaller)
{
    REQUIRE_SSHD(sshd);
    Connected c(sshd);
    ASSERT_TRUE(c.ok);
    SshExecResult result;
    QString error;
    QElapsedTimer clock;
    clock.start();

    EXPECT_FALSE(c.session.exec(QStringLiteral("sleep 20"), {}, 700, &result, &error));

    EXPECT_EQ(error, QStringLiteral("timed out"));
    EXPECT_LT(clock.elapsed(), 5000);
}

TEST(Libssh2Session, KeyProtectedByAPassphrase)
{
    REQUIRE_SSHD(sshd);
    QString error;
    {
        Libssh2Session session;
        ASSERT_TRUE(session.connectToHost(QStringLiteral("127.0.0.1"), sshd.port(), 10000, &error));
        EXPECT_FALSE(session.authenticate(sshd.access(QStringLiteral("locked"), QStringLiteral("wrong")), &error));
        EXPECT_TRUE(error.contains(QStringLiteral("passphrase"))) << qPrintable(error);
    }
    Libssh2Session session;
    ASSERT_TRUE(session.connectToHost(QStringLiteral("127.0.0.1"), sshd.port(), 10000, &error));
    EXPECT_TRUE(session.authenticate(sshd.access(QStringLiteral("locked"), QStringLiteral("correct horse")), &error))
        << qPrintable(error);
}

TEST(Libssh2Session, UnknownKeyIsRejectedWithAnExplanation)
{
    REQUIRE_SSHD(sshd);
    Libssh2Session session;
    QString error;
    ASSERT_TRUE(session.connectToHost(QStringLiteral("127.0.0.1"), sshd.port(), 10000, &error));

    EXPECT_FALSE(session.authenticate(sshd.access(QStringLiteral("stranger")), &error));

    EXPECT_FALSE(error.isEmpty());
}

TEST(Libssh2Session, NothingToSignInWithIsExplained)
{
    REQUIRE_SSHD(sshd);
    Libssh2Session session;
    QString error;
    ASSERT_TRUE(session.connectToHost(QStringLiteral("127.0.0.1"), sshd.port(), 10000, &error));
    SshAccess access = sshd.access();
    access.keyFile.clear();
    // Без ключа и пароля остаётся ssh-agent; тест не должен зависеть от того, запущен ли он у
    // того, кто гоняет тесты, поэтому проверяется только форма ответа.
    const bool ok = session.authenticate(access, &error);
    if (!ok)
        EXPECT_TRUE(error.contains(QStringLiteral("password or a key file"))) << qPrintable(error);
}

TEST(Libssh2Session, KnownHostsOfTheSystemAreConsulted)
{
    REQUIRE_SSHD(sshd);
    TempDir home;
    QDir(home.path()).mkpath(QStringLiteral(".ssh"));
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", home.path().toLocal8Bit());

    Libssh2Session session;
    QString error;
    ASSERT_TRUE(session.connectToHost(QStringLiteral("127.0.0.1"), sshd.port(), 10000, &error));

    // Файла нет.
    EXPECT_EQ(session.checkSystemKnownHosts(QStringLiteral("127.0.0.1"), sshd.port()), SystemHostKey::NotFound);

    auto writeKnownHosts = [&](const QString &pubName) {
        QFile pub(sshd.path(pubName));
        ASSERT_TRUE(pub.open(QIODevice::ReadOnly));
        const QList<QByteArray> parts = pub.readAll().split(' ');
        QFile out(home.filePath(QStringLiteral(".ssh/known_hosts")));
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        out.write(QStringLiteral("[127.0.0.1]:%1 %2 %3\n").arg(sshd.port()).arg(QString::fromLatin1(parts[0]), QString::fromLatin1(parts[1])).toUtf8());
    };

    writeKnownHosts(QStringLiteral("host.pub"));
    EXPECT_EQ(session.checkSystemKnownHosts(QStringLiteral("127.0.0.1"), sshd.port()), SystemHostKey::Match);

    writeKnownHosts(QStringLiteral("stranger.pub")); // та же запись, ключ другой
    EXPECT_EQ(session.checkSystemKnownHosts(QStringLiteral("127.0.0.1"), sshd.port()), SystemHostKey::Mismatch);

    qputenv("HOME", oldHome);
}

TEST(Libssh2Session, ConnectionErrorsAreReadable)
{
    quint16 closedPort = 0;
    {
        QTcpServer server;
        ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
        closedPort = server.serverPort();
    }
    Libssh2Session session;
    QString error;
    EXPECT_FALSE(session.connectToHost(QStringLiteral("127.0.0.1"), closedPort, 3000, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(Libssh2Session, ServerThatIsNotSshTimesOutOnTheHandshake)
{
    // Сервер принимает и молчит: так выглядит любой не-ssh сервис на порту 22.
    QTcpServer silent;
    ASSERT_TRUE(silent.listen(QHostAddress::LocalHost, 0));
    Libssh2Session session;
    QString error;
    QElapsedTimer clock;
    clock.start();

    EXPECT_FALSE(session.connectToHost(QStringLiteral("127.0.0.1"), silent.serverPort(), 1000, &error));

    EXPECT_TRUE(error.contains(QStringLiteral("handshake"))) << qPrintable(error);
    EXPECT_LT(clock.elapsed(), 6000);
}
