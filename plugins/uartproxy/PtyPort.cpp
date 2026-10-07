/**
 * \file PtyPort.cpp
 * \brief Реализация spotty::PtyPort.
 */
#include "PtyPort.h"

#include "DevLink.h"
#include "UartProxySettings.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSocketNotifier>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

namespace spotty {

namespace {

/**
 * \brief Предел очереди на запись программе, байт.
 *
 * Мегабайта хватает на всплеск, но не на бесконечно молчащего читателя. Буфер самого pty
 * мал (около килобайта на macOS), поэтому очередь здесь неизбежна, а предел — защита
 * от неё же.
 */
constexpr qsizetype kMaxPendingBytes = 1 << 20;

constexpr int kReadChunk = 4096;

/// \brief ptsname() возвращает указатель на статический буфер: вызывать под замком.
QString slaveNameOf(int master)
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    const char *name = ::ptsname(master);
    return name ? QString::fromLocal8Bit(name) : QString();
}

QString systemMessage(const char *what)
{
    return QStringLiteral("%1: %2").arg(QLatin1String(what),
                                        QString::fromLocal8Bit(std::strerror(errno)));
}

} // namespace

PtyPort::PtyPort(QString linkPath, QObject *parent)
    : VirtualPort(parent)
    , m_linkPath(std::move(linkPath))
{
}

PtyPort::~PtyPort()
{
    close();
}

bool PtyPort::open(const QVariantMap &settings, QString *error)
{
    Q_UNUSED(settings);

    const auto fail = [this, error](const QString &message) {
        if (error)
            *error = tr("Cannot create a virtual port (%1).").arg(message);
        close();
        return false;
    };

    m_master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (m_master < 0)
        return fail(systemMessage("posix_openpt"));
    if (::grantpt(m_master) != 0)
        return fail(systemMessage("grantpt"));
    if (::unlockpt(m_master) != 0)
        return fail(systemMessage("unlockpt"));

    m_slavePath = slaveNameOf(m_master);
    if (m_slavePath.isEmpty())
        return fail(systemMessage("ptsname"));

    // Не наследовать дескрипторы дочерними процессами и не блокироваться на чтении:
    // читает цикл событий, а запись не должна останавливать поток.
    ::fcntl(m_master, F_SETFD, FD_CLOEXEC);
    ::fcntl(m_master, F_SETFL, ::fcntl(m_master, F_GETFL) | O_NONBLOCK);

    m_slave = ::open(m_slavePath.toLocal8Bit().constData(), O_RDWR | O_NOCTTY);
    if (m_slave < 0)
        return fail(systemMessage("open slave"));
    ::fcntl(m_slave, F_SETFD, FD_CLOEXEC);

    termios attributes{};
    if (::tcgetattr(m_slave, &attributes) == 0) {
        ::cfmakeraw(&attributes);
        ::tcsetattr(m_slave, TCSANOW, &attributes);
    }

    if (!m_linkPath.isEmpty()) {
        QString linkError;
        if (uartproxy::isDevicePath(m_linkPath)) {
            // Имя в `/dev`: чужая программа найдёт его в списке портов. Сначала то, что
            // можно сделать без прав, — промежуточная ссылка на pty, — и лишь затем, если
            // имени ещё нет, запрос прав на ссылку в `/dev`.
            if (!uartproxy::deviceLinksSupported()) {
                return fail(tr("This system does not allow creating names in /dev, even with "
                               "administrator rights. Use a path outside /dev, for example "
                               "%1.")
                                .arg(QLatin1String(uartproxy::kUnprivilegedLinkPath)));
            }
            if (!uartproxy::isValidDevicePath(m_linkPath))
                return fail(tr("\"%1\" is not a valid name in /dev.").arg(m_linkPath));
            if (uartproxy::inspectDeviceLink(m_linkPath) == uartproxy::LinkState::Foreign) {
                return fail(
                    tr("%1 is already used by something else: choose another name.")
                        .arg(m_linkPath));
            }
            if (!createLink(uartproxy::bridgePath(), &linkError)
                || !uartproxy::ensureDeviceLink(m_linkPath, &linkError)) {
                return fail(linkError);
            }
            m_linkCreated = true;
        } else {
            if (!createLink(m_linkPath, &linkError))
                return fail(linkError);
            m_linkCreated = true;
        }
    }

    m_readNotifier = new QSocketNotifier(m_master, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &PtyPort::readAvailable);

    m_writeNotifier = new QSocketNotifier(m_master, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &PtyPort::flushPending);

    return true;
}

bool PtyPort::createLink(const QString &path, QString *error)
{
    const QByteArray link = path.toLocal8Bit();

    struct stat info{};
    if (::lstat(link.constData(), &info) == 0) {
        // Только своё: затирать файл пользователя ради удобства нельзя. Ссылка же от
        // прошлого запуска — наша, и её место свободно.
        if (!S_ISLNK(info.st_mode)) {
            *error = tr("%1 already exists and is not a link.").arg(path);
            return false;
        }
        ::unlink(link.constData());
    }

    if (::symlink(m_slavePath.toLocal8Bit().constData(), link.constData()) != 0) {
        *error = tr("Cannot create %1: %2")
                     .arg(path, QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    m_ownLink = path;
    return true;
}

void PtyPort::removeLink()
{
    // Ссылка в `/dev` остаётся: убрать её можно только с правами, а спрашивать их ещё раз
    // при каждом закрытии — значит мучить человека ради ссылки, которая и так исчезнет при
    // перезагрузке и никуда не ведёт, пока Spotty не запущен.
    m_linkCreated = false;
    if (m_ownLink.isEmpty())
        return;
    const QString link = m_ownLink;
    m_ownLink.clear();

    // Удаляется, только если указывает ещё на наш порт: за время работы ссылку мог
    // переложить другой экземпляр Spotty, и убрать чужую значило бы оборвать его.
    char target[1024];
    const QByteArray linkPath = link.toLocal8Bit();
    const ssize_t length = ::readlink(linkPath.constData(), target, sizeof(target) - 1);
    if (length > 0 && QString::fromLocal8Bit(target, int(length)) == m_slavePath)
        ::unlink(linkPath.constData());
}

void PtyPort::close()
{
    removeLink();

    delete m_readNotifier;
    m_readNotifier = nullptr;
    delete m_writeNotifier;
    m_writeNotifier = nullptr;

    if (m_slave >= 0)
        ::close(m_slave);
    if (m_master >= 0)
        ::close(m_master);
    m_slave = -1;
    m_master = -1;

    m_slavePath.clear();
    m_pending.clear();
    m_overflowReported = false;
}

QString PtyPort::description() const
{
    return m_linkCreated ? m_linkPath : m_slavePath;
}

QString PtyPort::details() const
{
    // Имя и сам порт рядом: имя выбирал пользователь, а настоящий путь меняется при каждом
    // запуске и нужен тем программам, что покажут его в списке сами.
    return m_linkCreated ? QStringLiteral("%1 (%2)").arg(m_linkPath, m_slavePath)
                         : m_slavePath;
}

void PtyPort::readAvailable()
{
    char buffer[kReadChunk];
    for (;;) {
        const ssize_t count = ::read(m_master, buffer, sizeof(buffer));
        if (count > 0) {
            Q_EMIT dataRead(QByteArray(buffer, int(count)));
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        // 0 и EIO: подчинённых концов не осталось. Собственный держится открытым, так что
        // сюда приходит только настоящий сбой.
        if (count < 0)
            Q_EMIT failed(tr("Virtual port failed: %1").arg(systemMessage("read")));
        return;
    }
}

void PtyPort::write(const QByteArray &data)
{
    if (m_master < 0 || data.isEmpty())
        return;

    if (m_pending.size() + data.size() > kMaxPendingBytes) {
        if (!m_overflowReported) {
            m_overflowReported = true;
            Q_EMIT overflowed();
        }
        return;
    }

    m_pending.append(data);
    flushPending();
}

void PtyPort::flushPending()
{
    while (!m_pending.isEmpty()) {
        const ssize_t count = ::write(m_master, m_pending.constData(), size_t(m_pending.size()));
        if (count > 0) {
            m_pending.remove(0, int(count));
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && errno != EAGAIN)
            Q_EMIT failed(tr("Virtual port failed: %1").arg(systemMessage("write")));
        break;
    }

    // Уведомление о готовности записи нужно, пока остаток не ушёл: иначе оно срабатывало бы
    // вхолостую на каждом обороте цикла событий.
    if (m_writeNotifier)
        m_writeNotifier->setEnabled(!m_pending.isEmpty());
    if (m_pending.isEmpty())
        m_overflowReported = false;
}

} // namespace spotty
