/**
 * \file DevLink.cpp
 * \brief Реализация постоянного имени виртуального порта в `/dev`.
 */
#include "DevLink.h"

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cerrno>

#include <sys/stat.h>
#include <unistd.h>

namespace spotty::uartproxy {

namespace {

QString g_bridgePath = QStringLiteral("/tmp/spotty-uart");
Elevator g_elevator;

QString tr(const char *text)
{
    return QCoreApplication::translate("spotty::UartProxyDevLink", text);
}

/// \brief Строка для `sh` в одинарных кавычках; проверенные пути в них попадают как есть.
QString shellQuote(const QString &text)
{
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

/// \brief Куда ведёт ссылка; пустая строка, если это не ссылка.
QString linkTarget(const QString &path)
{
    char buffer[1024];
    const ssize_t length = ::readlink(path.toLocal8Bit().constData(), buffer, sizeof(buffer) - 1);
    return length > 0 ? QString::fromLocal8Bit(buffer, int(length)) : QString();
}

bool pathExists(const QString &path)
{
    struct stat info{};
    return ::lstat(path.toLocal8Bit().constData(), &info) == 0;
}

bool runWithPrivileges(const QString &script, bool *cancelled, QString *error)
{
    if (cancelled)
        *cancelled = false;
    if (g_elevator)
        return g_elevator(script, cancelled, error);

    QString program;
    QStringList arguments;
    int cancelCode = -1;

#if defined(Q_OS_MACOS)
    // Сюда не доходят: см. deviceLinksSupported(). Ветка оставлена, чтобы ошибка была
    // внятной, если кто-то вызовет создание ссылки в обход проверки.
    if (error)
        *error = tr("macOS does not allow creating names in /dev.");
    return false;
#else
    program = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (program.isEmpty()) {
        if (error)
            *error = tr("pkexec is not installed, so Spotty cannot ask for administrator "
                        "rights. Choose a path outside /dev or create the link yourself.");
        return false;
    }
    arguments = {QStringLiteral("/bin/sh"), QStringLiteral("-c"), script};
    cancelCode = 126; // pkexec: окно закрыто или доступ не подтверждён
#endif

    QProcess process;
    process.start(program, arguments);
    // Пока человек вводит пароль, поток ввода-вывода ждёт: открытие канала всё равно не
    // продолжить без ответа, а остальным интерфейсам он не мешает — у каждого свой поток.
    constexpr int kPasswordTimeoutMs = 120 * 1000;
    if (!process.waitForStarted() || !process.waitForFinished(kPasswordTimeoutMs)) {
        process.kill();
        if (error)
            *error = tr("Could not run %1.").arg(program);
        return false;
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
        return true;

    const QString output = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    const bool declined = cancelCode >= 0 && process.exitCode() == cancelCode;
    if (declined) {
        if (cancelled)
            *cancelled = true;
        return false;
    }
    if (error)
        *error = output.isEmpty() ? tr("The command failed.") : output;
    return false;
}

} // namespace

QString bridgePath()
{
    return g_bridgePath;
}

void setBridgePathForTesting(const QString &path)
{
    g_bridgePath = path.isEmpty() ? QStringLiteral("/tmp/spotty-uart") : path;
}

void setElevatorForTesting(Elevator elevator)
{
    g_elevator = std::move(elevator);
}

bool deviceLinksSupported()
{
#ifdef Q_OS_MACOS
    return false;
#else
    return true;
#endif
}

bool isDevicePath(const QString &path)
{
    return path.startsWith(QLatin1String("/dev/"));
}

bool isValidDevicePath(const QString &path)
{
    static const QRegularExpression pattern(
        QStringLiteral("^/dev/[A-Za-z0-9][A-Za-z0-9._-]*$"));
    return pattern.match(path).hasMatch() && !path.contains(QLatin1String(".."));
}

LinkState inspectLink(const QString &path, const QString &target)
{
    if (!pathExists(path))
        return LinkState::Missing;
    return linkTarget(path) == target ? LinkState::Ready : LinkState::Foreign;
}

LinkState inspectDeviceLink(const QString &path)
{
    return inspectLink(path, bridgePath());
}

bool ensureDeviceLink(const QString &path, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!isValidDevicePath(path))
        return fail(tr("\"%1\" is not a valid name in /dev.").arg(path));

    switch (inspectDeviceLink(path)) {
    case LinkState::Ready:
        return true;
    case LinkState::Foreign:
        return fail(tr("%1 is already used by something else: choose another name.").arg(path));
    case LinkState::Missing:
        break;
    }

    // `ln` без подстановки: все части уже проверены, а существующее чужое сюда не доходит.
    const QString script =
        QStringLiteral("/bin/ln -s %1 %2").arg(shellQuote(bridgePath()), shellQuote(path));

    bool cancelled = false;
    QString problem;
    if (!runWithPrivileges(script, &cancelled, &problem)) {
        return fail(cancelled ? tr("Administrator rights are needed to create %1.").arg(path)
                              : problem);
    }

    // Права получены, а ссылки нет — значит, система такое имя не приняла.
    if (inspectDeviceLink(path) != LinkState::Ready)
        return fail(tr("%1 was not created.").arg(path));
    return true;
}

bool removeDeviceLink(const QString &path, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!isValidDevicePath(path))
        return fail(tr("\"%1\" is not a valid name in /dev.").arg(path));

    switch (inspectDeviceLink(path)) {
    case LinkState::Missing:
        return true;
    case LinkState::Foreign:
        // Чужое удалять нельзя ни при каких обстоятельствах, тем более с правами root.
        return fail(tr("%1 was not created by Spotty and is left untouched.").arg(path));
    case LinkState::Ready:
        break;
    }

    const QString script = QStringLiteral("/bin/rm %1").arg(shellQuote(path));
    bool cancelled = false;
    QString problem;
    if (!runWithPrivileges(script, &cancelled, &problem)) {
        return fail(cancelled ? tr("Administrator rights are needed to remove %1.").arg(path)
                              : problem);
    }
    return true;
}

} // namespace spotty::uartproxy
