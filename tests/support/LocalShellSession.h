/**
 * \file LocalShellSession.h
 * \brief Подставной ssh-сеанс, «удалённая машина» которого — локальный shell в песочнице.
 */
#pragma once

#include "SshSession.h"
#include "AgentPackage.h"

#include "support/TestSupport.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>

#include <functional>

namespace spotty::test {

/**
 * \class LocalShellSession
 * \brief Выполняет команды установщика настоящим `sh`, но «на удалённой машине» с корнем в песочнице.
 *
 * Скрипты установки запускаются те же, что уйдут на плату, а от настоящей системы их
 * отделяют переменные окружения (`SPOTTY_ROOT`, `SPOTTY_SYSTEMD`, `SPOTTY_SYSTEMCTL`) и два
 * заменителя в `PATH`: `sudo` проверяет пароль, а `systemctl` пишет вызовы в журнал. Благодаря
 * этому проверяется то, что иначе проверить нечем без железа: порядок шагов, права, пароль в
 * канале ввода, уборка за собой и сам запуск агента.
 */
class LocalShellSession : public tcptunnel::ISshSession
{
public:
    explicit LocalShellSession(const QString &sandbox)
        : m_sandbox(sandbox)
    {
        QDir().mkpath(root());
        QDir().mkpath(home());
        QDir().mkpath(binDir());
        writeStub(QStringLiteral("sudo"),
                  "#!/bin/sh\n"
                  "# Заменитель sudo: -S читает пароль первой строкой ввода, дальше выполняет команду.\n"
                  "while [ $# -gt 0 ]; do\n"
                  "  case $1 in -S|-n) shift;; -p) shift 2;; --) shift; break;; *) break;; esac\n"
                  "done\n"
                  "IFS= read -r password\n"
                  "if [ \"$password\" != \"$SPOTTY_TEST_SUDO_PASSWORD\" ]; then\n"
                  "  echo 'Sorry, try again.' >&2\n"
                  "  echo 'sudo: 1 incorrect password attempt' >&2\n"
                  "  exit 1\n"
                  "fi\n"
                  "exec \"$@\"\n");
        writeStub(QStringLiteral("systemctl"),
                  "#!/bin/sh\n"
                  "echo \"$*\" >> \"$SPOTTY_TEST_SYSTEMCTL_LOG\"\n"
                  "exit 0\n");
    }

    // --- Что настраивает тест -------------------------------------------------------------

    QString fingerprint = QStringLiteral("SHA256:testfingerprint");
    tcptunnel::SystemHostKey systemKey = tcptunnel::SystemHostKey::NotFound;
    QString connectError;
    QString authError;
    QString sudoPassword = QStringLiteral("secret");
    bool systemd = false;

    /// \brief Вместо запуска `sh -s` отдать этот вывод detect.sh (иначе скрипт выполнится на самом деле).
    QString cannedDetect;

    /// \brief Перехват команд: вернуть `true` и заполнить результат, чтобы подменить выполнение.
    std::function<bool(const QString &command, tcptunnel::SshExecResult *result)> intercept;

    // --- Что записал сеанс ----------------------------------------------------------------

    QStringList commands;
    QByteArray lastDetectInput;
    QList<tcptunnel::SshAccess> authAttempts;
    int connectCount = 0;

    QString root() const { return QDir(m_sandbox).filePath(QStringLiteral("root")); }
    QString home() const { return QDir(m_sandbox).filePath(QStringLiteral("home")); }
    QString binDir() const { return QDir(m_sandbox).filePath(QStringLiteral("bin")); }
    QString systemctlLog() const { return QDir(m_sandbox).filePath(QStringLiteral("systemctl.log")); }

    /// \brief Путь внутри «корня удалённой машины».
    QString remotePath(const QString &path) const { return root() + path; }

    QStringList systemctlCalls() const
    {
        QFile file(systemctlLog());
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    // --- ISshSession ----------------------------------------------------------------------

    bool connectToHost(const QString &, int, int, QString *error) override
    {
        ++connectCount;
        if (!connectError.isEmpty()) {
            *error = connectError;
            return false;
        }
        return true;
    }

    QString hostKeyFingerprint() const override { return fingerprint; }

    tcptunnel::SystemHostKey checkSystemKnownHosts(const QString &, int) override { return systemKey; }

    bool authenticate(const tcptunnel::SshAccess &access, QString *error) override
    {
        authAttempts.append(access);
        if (!authError.isEmpty()) {
            *error = authError;
            return false;
        }
        return true;
    }

    bool exec(const QString &command, const QByteArray &input, int timeoutMs,
              tcptunnel::SshExecResult *result, QString *error) override
    {
        commands.append(command);
        if (command == QLatin1String("sh -s"))
            lastDetectInput = input;
        if (command == QLatin1String("sh -s") && !cannedDetect.isEmpty()) {
            *result = {0, cannedDetect.toUtf8(), {}};
            return true;
        }
        if (intercept && intercept(command, result))
            return true;

        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PATH"), binDir() + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
        env.insert(QStringLiteral("HOME"), home());
        env.insert(QStringLiteral("SPOTTY_ROOT"), root());
        env.insert(QStringLiteral("SPOTTY_SYSTEMD"), systemd ? QStringLiteral("yes") : QStringLiteral("no"));
        env.insert(QStringLiteral("SPOTTY_SYSTEMCTL"), QDir(binDir()).filePath(QStringLiteral("systemctl")));
        env.insert(QStringLiteral("SPOTTY_TEST_SUDO_PASSWORD"), sudoPassword);
        env.insert(QStringLiteral("SPOTTY_TEST_SYSTEMCTL_LOG"), systemctlLog());
        process.setProcessEnvironment(env);
        process.start(QStringLiteral("sh"), {QStringLiteral("-c"), command});
        if (!process.waitForStarted(5000)) {
            *error = QStringLiteral("cannot start sh");
            return false;
        }
        process.write(input);
        process.closeWriteChannel();
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            *error = QStringLiteral("timed out");
            return false;
        }
        result->exitCode = process.exitCode();
        result->out = process.readAllStandardOutput();
        result->err = process.readAllStandardError();
        return true;
    }

    void disconnectFromHost() override {}

private:
    void writeStub(const QString &name, const char *body)
    {
        QFile file(QDir(binDir()).filePath(name));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(body);
            file.close();
            file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup
                                | QFileDevice::ExeOther);
        }
    }

    QString m_sandbox;
};

/**
 * \class NativeAgentPackage
 * \brief Комплект, в котором под любую «архитектуру» отдаётся агент, собранный для этой машины.
 *
 * Бинарники под Linux на тестовой машине не запустить, а проверять нужно именно запуск; скрипты
 * и шаблоны берутся настоящие из каталога исходников.
 */
class NativeAgentPackage : public tcptunnel::AgentPackage
{
public:
    NativeAgentPackage(const QString &nativeBinary, const QString &agentDir, const QString &binariesDir)
        : AgentPackage(binariesDir, agentDir)
        , m_nativeBinary(nativeBinary)
    {
    }

    QByteArray binary(const QString &arch) const override
    {
        return arch.isEmpty() ? QByteArray{} : tcptunnel::readAgentFile(m_nativeBinary);
    }

private:
    QString m_nativeBinary;
};

} // namespace spotty::test
