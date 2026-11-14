/**
 * \file test_update_helper.cpp
 * \brief Проверка замены и отката во временном каталоге.
 */
#include "../src/ui/UpdateScripts.h"

#include <gtest/gtest.h>

#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>

using namespace spotty;

#if defined(Q_OS_UNIX)
namespace {

bool putFile(const QString &path, const QByteArray &contents, bool executable = false)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size())
        return false;
    file.close();
    return !executable || file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                               | QFileDevice::ExeOwner);
}

void checkUpdate(bool failRename, bool failStart)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    QDir root(temp.path());
    ASSERT_TRUE(root.mkpath(QStringLiteral("work")));
    ASSERT_TRUE(root.mkpath(QStringLiteral("tools")));
    ASSERT_TRUE(root.mkpath(QStringLiteral("config")));
    const QString target = root.filePath(QStringLiteral("Spotty AppImage"));
    const QString payload = root.filePath(QStringLiteral("work/download"));
    const QString script = root.filePath(QStringLiteral("work/update.sh"));
    const QString marker = root.filePath(QStringLiteral("launched"));
    const QByteArray oldApp = "#!/bin/sh\nprintf old > \"$SPOTTY_UPDATE_TEST_MARKER\"\n";
    const QByteArray newApp = failStart
        ? "#!/bin/sh\nexit 1\n"
        : "#!/bin/sh\n: > \"$2\"\nprintf new > \"$SPOTTY_UPDATE_TEST_MARKER\"\n";
    ASSERT_TRUE(putFile(target, oldApp, true));
    ASSERT_TRUE(putFile(payload, newApp, true));
    ASSERT_TRUE(putFile(script, update_scripts::kPosixHelper));
    ASSERT_TRUE(putFile(root.filePath(QStringLiteral("config/settings.json")), "user data"));

    // \brief На macOS системный chmod не поддерживает Linux-ключ --reference.
    ASSERT_TRUE(putFile(root.filePath(QStringLiteral("tools/chmod")),
                        "#!/bin/sh\ncase \"$1\" in --reference=*) exec /bin/chmod 755 \"$2\";; "
                        "*) exec /bin/chmod \"$@\";; esac\n", true));
    if (failRename) {
        ASSERT_TRUE(putFile(root.filePath(QStringLiteral("tools/mv")),
                            "#!/bin/sh\ncase \"$1\" in */.spotty-new-*) exit 1;; "
                            "*) exec /bin/mv \"$@\";; esac\n", true));
    }
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), root.filePath(QStringLiteral("tools")) + u':'
                                           + env.value(QStringLiteral("PATH")));
    env.insert(QStringLiteral("SPOTTY_UPDATE_TEST_MARKER"), marker);
    process.setProcessEnvironment(env);
    process.start(QStringLiteral("/bin/sh"),
                  {script, QStringLiteral("99999999"), payload, target,
                   root.filePath(QStringLiteral("work")), QStringLiteral("linux"),
                   QStringLiteral("update failed")});
    ASSERT_TRUE(process.waitForFinished(15000));
    ASSERT_EQ(process.exitCode(), 0) << process.readAllStandardError().constData();

    const QByteArray expected = failRename || failStart ? QByteArrayLiteral("old")
                                                        : QByteArrayLiteral("new");
    QByteArray launched;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2000 && launched != expected) {
        QFile result(marker);
        if (result.open(QIODevice::ReadOnly))
            launched = result.readAll();
        if (launched != expected)
            QThread::msleep(20);
    }
    EXPECT_EQ(launched, expected);
    QFile current(target);
    ASSERT_TRUE(current.open(QIODevice::ReadOnly));
    EXPECT_EQ(current.readAll(), failRename || failStart ? oldApp : newApp);
    QFile settings(root.filePath(QStringLiteral("config/settings.json")));
    ASSERT_TRUE(settings.open(QIODevice::ReadOnly));
    EXPECT_EQ(settings.readAll(), QByteArrayLiteral("user data"));
    EXPECT_EQ(root.exists(QStringLiteral("work")), failRename || failStart);
}

} // namespace

TEST(UpdateHelper, ReplacesAndStartsNewVersion)
{
    checkUpdate(false, false);
}

TEST(UpdateHelper, RestoresAfterRenameFailure)
{
    checkUpdate(true, false);
}

TEST(UpdateHelper, RestoresWhenNewVersionDoesNotStart)
{
    checkUpdate(false, true);
}
#endif
