/**
 * \file RemoteInstaller.cpp
 * \brief Реализация spotty::tcptunnel::RemoteInstaller.
 */
#include "RemoteInstaller.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStringList>

#include <algorithm>

namespace spotty::tcptunnel {

namespace {

constexpr int kQuickMs = 20000;
constexpr int kInstallMs = 90000;

/// \brief Первая непустая строка вывода — для коротких сообщений об ошибке.
QString firstLine(const QByteArray &text)
{
    const QStringList lines = QString::fromUtf8(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.first().trimmed();
}

/// \brief Каталог, который создали мы сами (см. makeStage()): только такой разрешено удалять.
bool isStageDir(const QString &path)
{
    static const QRegularExpression safe(QStringLiteral("^/[A-Za-z0-9_./-]+/\\.spotty-stage\\.[A-Za-z0-9]+$"));
    return safe.match(path).hasMatch();
}

} // namespace

QString lastScriptError(const QString &scriptOutput)
{
    QString found;
    for (const QString &line : scriptOutput.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("ERROR:")))
            found = line.mid(6).trimmed();
    }
    return found;
}

/// \brief Почему команда не удалась: строка `ERROR:` скрипта, иначе первая строка stderr, иначе код возврата.
static QString failureReason(const SshExecResult &result)
{
    QString reason = lastScriptError(QString::fromUtf8(result.err));
    if (reason.isEmpty())
        reason = firstLine(result.err);
    return reason.isEmpty() ? RemoteInstaller::tr("exit code %1").arg(result.exitCode) : reason;
}

RemoteInstaller::RemoteInstaller(ISshSession &session, const AgentPackage &package,
                                 KnownHosts &knownHosts, Hooks hooks)
    : m_session(session)
    , m_package(package)
    , m_knownHosts(knownHosts)
    , m_hooks(std::move(hooks))
{
}

void RemoteInstaller::progress(const QString &text) const
{
    if (m_hooks.progress)
        m_hooks.progress(text);
}

bool RemoteInstaller::cancelled() const
{
    return m_hooks.cancelled && m_hooks.cancelled();
}

bool RemoteInstaller::connect(const SshAccess &access, QString *error)
{
    m_access = access;
    m_refusedFingerprint.clear();
    if (access.host.trimmed().isEmpty()) {
        *error = tr("Enter the address of the machine first (the Host field).");
        return false;
    }
    if (access.user.trimmed().isEmpty()) {
        *error = tr("Enter the ssh user name.");
        return false;
    }

    progress(tr("Connecting to %1:%2...").arg(access.host).arg(access.port));
    QString reason;
    if (!m_session.connectToHost(access.host, access.port, access.timeoutMs, &reason)) {
        *error = tr("Cannot connect to %1:%2 over ssh: %3").arg(access.host).arg(access.port).arg(reason);
        return false;
    }

    // Ключ хоста проверяется до того, как на этот хост уйдёт пароль.
    const QString fingerprint = m_session.hostKeyFingerprint();
    const SystemHostKey system = m_session.checkSystemKnownHosts(access.host, access.port);
    const KnownHosts::Match known = m_knownHosts.check(access.host, access.port, fingerprint);

    if (system == SystemHostKey::Mismatch) {
        *error = tr("The ssh host key of %1 does not match the one in your ~/.ssh/known_hosts "
                    "(now %2). Someone may be intercepting the connection, or the machine was "
                    "reinstalled. Nothing was sent. Fix known_hosts if you are sure.")
                     .arg(access.host, fingerprint);
        return false;
    }
    if (known == KnownHosts::Match::Different) {
        *error = tr("The ssh host key of %1 has CHANGED (now %2). Someone may be intercepting "
                    "the connection, or the machine was reinstalled. Nothing was sent. If you are "
                    "sure, press \"Forget the saved host key\" and try again.")
                     .arg(access.host, fingerprint);
        return false;
    }
    if (system != SystemHostKey::Match && known != KnownHosts::Match::Same) {
        if (access.trustFingerprint != fingerprint) {
            m_refusedFingerprint = fingerprint;
            *error = tr("The ssh host key of %1 is not known yet:\n%2\nCheck that this is your "
                        "machine (on it: ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub). If it "
                        "matches, press the same button again to trust this machine.")
                         .arg(access.host, fingerprint);
            return false;
        }
        m_knownHosts.remember(access.host, access.port, fingerprint);
        progress(tr("Saved the host key %1").arg(fingerprint));
    }

    progress(tr("Signing in as %1...").arg(access.user));
    if (!m_session.authenticate(access, &reason)) {
        *error = tr("Cannot sign in as %1: %2").arg(access.user, reason);
        return false;
    }
    return true;
}

bool RemoteInstaller::run(const QString &command, const QByteArray &input, int timeoutMs,
                          SshExecResult *result, QString *error)
{
    QString reason;
    if (!m_session.exec(command, input, timeoutMs, result, &reason)) {
        *error = reason.isEmpty() ? tr("The command did not complete.") : reason;
        return false;
    }
    return true;
}

bool RemoteInstaller::runPrivileged(const HostInfo &info, const QString &command, int timeoutMs,
                                    SshExecResult *result, QString *error)
{
    if (info.uid == 0)
        return run(command, {}, timeoutMs, result, error);

    if (!info.hasSudo) {
        *error = tr("You are signed in as %1, not root, and sudo is not installed on the machine. "
                    "Sign in as root or install sudo.")
                     .arg(m_access.user);
        return false;
    }

    // -S: пароль со стандартного ввода; -p '': без приглашения, чтобы оно не попало в вывод.
    // Пароль уходит в канал, а не в командную строку — её видно в списке процессов.
    const QByteArray input = (m_access.password + QLatin1Char('\n')).toUtf8();
    if (!run(QStringLiteral("sudo -S -p '' ") + command, input, timeoutMs, result, error))
        return false;

    if (result->exitCode != 0) {
        const QString err = QString::fromUtf8(result->err);
        if (err.contains(QLatin1String("incorrect password")) || err.contains(QLatin1String("Sorry, try again"))
            || err.contains(QLatin1String("a password is required"))
            || err.contains(QLatin1String("no tty present"))) {
            *error = tr("sudo did not accept the password. Enter the password of the account %1 in "
                        "the Password field - it is used for sudo as well, also when you sign in "
                        "with a key.")
                         .arg(m_access.user);
            return false;
        }
        if (err.contains(QLatin1String("not in the sudoers")) || err.contains(QLatin1String("may not run sudo"))) {
            *error = tr("The user %1 is not allowed to use sudo on this machine. Sign in as root or "
                        "as a user with sudo rights.")
                         .arg(m_access.user);
            return false;
        }
    }
    return true;
}

bool RemoteInstaller::probe(HostInfo *info, QString *error)
{
    progress(tr("Looking at the machine..."));
    const QByteArray script = m_package.file(QStringLiteral("detect.sh"));
    if (script.isEmpty()) {
        *error = tr("The detection script is missing from this build of Spotty.");
        return false;
    }

    SshExecResult result;
    // «sh -s» читает скрипт со стандартного ввода: заливать его файлом ради одного запуска
    // незачем.
    if (!run(QStringLiteral("sh -s"), script, kQuickMs, &result, error))
        return false;
    if (result.exitCode != 0) {
        *error = tr("Could not examine the machine: %1").arg(failureReason(result));
        return false;
    }

    *info = parseHostInfo(QString::fromUtf8(result.out));
    progress(tr("Machine: %1").arg(info->summary()));
    return true;
}

bool RemoteInstaller::makeStage(const QString &base, QString *dir, QString *error)
{
    // mktemp создаёт каталог с правами 0700 и случайным именем: чужой пользователь не подложит
    // файл под наше имя до запуска от root.
    SshExecResult result;
    if (!run(QStringLiteral("mktemp -d \"%1/.spotty-stage.XXXXXX\"").arg(base), {}, kQuickMs, &result, error))
        return false;
    const QString path = firstLine(result.out);
    if (result.exitCode != 0 || !isStageDir(path)) {
        *error = tr("Cannot create a temporary directory in %1 on the machine.").arg(base);
        return false;
    }
    *dir = path;
    return true;
}

bool RemoteInstaller::put(const QString &path, const QByteArray &data, int mode, QString *error)
{
    SshExecResult result;
    const QString command = QStringLiteral("cat > %1 && chmod %2 %1")
                                .arg(shellQuote(path), QString::number(mode, 8));
    if (!run(command, data, kQuickMs, &result, error))
        return false;
    if (result.exitCode != 0) {
        *error = tr("Cannot write %1 on the machine: %2").arg(path, failureReason(result));
        return false;
    }
    return true;
}

void RemoteInstaller::removeStage(const QString &dir)
{
    // Только то, что создали сами: путь проверяется тем же шаблоном, что и при создании.
    if (!isStageDir(dir))
        return;
    SshExecResult result;
    QString ignored;
    run(QStringLiteral("rm -rf %1").arg(shellQuote(dir)), {}, kQuickMs, &result, &ignored);
}

bool RemoteInstaller::prepareAgentStage(const HostInfo &info, QString *dir, QString *error)
{
    if (info.agentArch.isEmpty()) {
        *error = info.archProblem.isEmpty() ? tr("No agent is built for this machine.") : info.archProblem;
        return false;
    }
    const QByteArray binary = m_package.binary(info.agentArch);
    if (binary.isEmpty()) {
        *error = tr("This build of Spotty has no agent for %1.").arg(info.agentArch);
        return false;
    }

    QString lastReason;
    // /tmp на части систем смонтирован с noexec — тогда залитый бинарник не запустится, и
    // повторяем в домашнем каталоге.
    for (const QString &base : {QStringLiteral("/tmp"), QStringLiteral("${HOME}")}) {
        QString stage;
        if (!makeStage(base, &stage, error)) {
            lastReason = *error;
            continue;
        }
        if (!put(stage + QStringLiteral("/spotty-uartd"), binary, 0755, error)) {
            removeStage(stage);
            return false;
        }

        SshExecResult result;
        if (!run(shellQuote(stage + QStringLiteral("/spotty-uartd")) + QStringLiteral(" --version"), {},
                 kQuickMs, &result, error)) {
            removeStage(stage);
            return false;
        }
        const QString version = QString::fromUtf8(result.out).trimmed();
        if (result.exitCode == 0 && version.contains(m_package.version())) {
            *dir = stage;
            return true;
        }
        removeStage(stage);
        lastReason = tr("The agent does not start here (\"%1\"). Detected: %2.")
                         .arg(failureReason(result), info.summary());
    }
    *error = lastReason;
    return false;
}

bool RemoteInstaller::listPorts(const QString &stage, QList<UartPort> *ports, QString *error)
{
    SshExecResult result;
    if (!run(shellQuote(stage + QStringLiteral("/spotty-uartd")) + QStringLiteral(" --scan"), {}, kQuickMs, &result,
             error)) {
        return false;
    }
    if (result.exitCode != 0) {
        *error = tr("The port search failed: %1").arg(failureReason(result));
        return false;
    }
    *ports = parseUartScan(QString::fromUtf8(result.out));
    return true;
}

bool RemoteInstaller::scanUarts(const HostInfo &info, QList<UartPort> *ports, QString *error)
{
    progress(tr("Looking for serial ports..."));
    QString stage;
    if (!prepareAgentStage(info, &stage, error))
        return false;
    const bool listed = listPorts(stage, ports, error);
    removeStage(stage);
    if (listed)
        progress(tr("Serial ports found: %1").arg(ports->size()));
    return listed;
}

bool RemoteInstaller::install(const HostInfo &info, const InstallParams &params, QString *error)
{
    const QString invalid = validateInstallParams(params);
    if (!invalid.isEmpty()) {
        *error = invalid;
        return false;
    }
    if (params.autostart && !info.systemd) {
        *error = tr("This machine does not run systemd, so the agent cannot be started at boot. "
                    "Untick \"Start at boot\" to run it until the next reboot.");
        return false;
    }

    const HostInfo::Instance *existing = info.instance(params.port);
    if (!existing && info.listening.contains(params.port)) {
        *error = tr("Port %1 on the machine is already used by another program. Choose another "
                    "port (%2 is free).")
                     .arg(params.port)
                     .arg(nextFreePort(info, params.port));
        return false;
    }

    QString stage;
    if (!prepareAgentStage(info, &stage, error))
        return false;

    // С любого выхода промежуточный каталог убирается.
    const auto cleanup = qScopeGuard([&] { removeStage(stage); });

    // Какой путь записать в конфиг. Номер ttyUSBn переназначается при каждом переподключении,
    // поэтому, когда у порта есть устойчивое имя, в конфиг идёт оно; введённый вручную путь,
    // которого сканер не знает, остаётся как есть. Не нашли порты — не беда: пишем, что ввели.
    QString device = params.device;
    QList<UartPort> ports;
    QString ignored;
    listPorts(stage, &ports, &ignored);
    for (const UartPort &port : std::as_const(ports)) {
        if ((port.device == params.device || port.stablePath == params.device) && !port.stablePath.isEmpty()) {
            device = port.stablePath;
            break;
        }
    }

    // Порт уже занят нашим агентом: перенастройка допустима, только если это тот же UART.
    // Молча перезаписать конфиг другого порта значило бы отнять его у того, кто им пользуется.
    if (existing) {
        // Один и тот же UART записывают и как /dev/ttyUSB0, и устойчивым именем.
        const auto isThisPort = [](const UartPort &port, const QString &path) {
            return path == port.device || path == port.stablePath;
        };
        const bool sameUart = existing->device == device || existing->device == params.device
                              || std::any_of(ports.cbegin(), ports.cend(), [&](const UartPort &port) {
                                     return isThisPort(port, existing->device) && isThisPort(port, params.device);
                                 });
        if (!sameUart) {
            *error = tr("Port %1 is already used by the agent for %2. Choose another port (%3 is "
                        "free) or remove that agent first.")
                         .arg(params.port)
                         .arg(existing->device)
                         .arg(nextFreePort(info, params.port));
            return false;
        }
    }

    if (cancelled()) {
        *error = tr("Cancelled.");
        return false;
    }

    InstallParams effective = params;
    effective.device = device;
    const QByteArray templ = m_package.file(QStringLiteral("uartd.conf.template"));
    const QByteArray unit = m_package.file(QStringLiteral("spotty-uartd@.service"));
    const QByteArray script = m_package.file(QStringLiteral("install.sh"));
    if (templ.isEmpty() || unit.isEmpty() || script.isEmpty()) {
        *error = tr("The installation files are missing from this build of Spotty.");
        return false;
    }

    progress(tr("Uploading the files..."));
    if (!put(stage + QStringLiteral("/spotty-uartd@.service"), unit, 0644, error)
        || !put(stage + QStringLiteral("/install.sh"), script, 0755, error)
        || !put(stage + QStringLiteral("/%1.conf").arg(params.port),
                renderConfig(QString::fromUtf8(templ), effective).toUtf8(), 0644, error)) {
        return false;
    }

    progress(tr("Installing..."));
    SshExecResult result;
    const QString command = QStringLiteral("sh %1 %2 %3")
                                .arg(shellQuote(stage + QStringLiteral("/install.sh")))
                                .arg(params.port)
                                .arg(params.autostart ? 1 : 0);
    if (!runPrivileged(info, command, kInstallMs, &result, error))
        return false;

    const QString output = QString::fromUtf8(result.out);
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("STEP:")))
            progress(line.mid(5).trimmed());
    }
    if (result.exitCode != 0) {
        *error = tr("Installation failed: %1").arg(failureReason(result));
        return false;
    }
    return true;
}

bool RemoteInstaller::uninstall(const HostInfo &info, int port, QString *error)
{
    if (!info.instance(port)) {
        *error = tr("There is no agent on port %1 of this machine.").arg(port);
        return false;
    }

    QString stage;
    if (!makeStage(QStringLiteral("/tmp"), &stage, error))
        return false;
    const auto cleanup = qScopeGuard([&] { removeStage(stage); });

    const QByteArray script = m_package.file(QStringLiteral("uninstall.sh"));
    if (script.isEmpty()) {
        *error = tr("The removal script is missing from this build of Spotty.");
        return false;
    }
    if (!put(stage + QStringLiteral("/uninstall.sh"), script, 0755, error))
        return false;

    progress(tr("Removing the agent..."));
    SshExecResult result;
    const QString command = QStringLiteral("sh %1 %2").arg(shellQuote(stage + QStringLiteral("/uninstall.sh"))).arg(port);
    if (!runPrivileged(info, command, kInstallMs, &result, error))
        return false;
    if (result.exitCode != 0) {
        *error = tr("Removal failed: %1").arg(failureReason(result));
        return false;
    }
    return true;
}

} // namespace spotty::tcptunnel
