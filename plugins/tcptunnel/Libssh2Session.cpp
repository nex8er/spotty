/**
 * \file Libssh2Session.cpp
 * \brief Реализация spotty::tcptunnel::Libssh2Session.
 */
#include "Libssh2Session.h"

#include <libssh2.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QStringList>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <cstdlib>
#include <cstring>
#include <memory>
#include <tuple>

namespace spotty::tcptunnel {

namespace {

#ifdef Q_OS_WIN
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
inline void closeNative(NativeSocket s) { ::closesocket(s); }
inline void setNonBlocking(NativeSocket s)
{
    u_long on = 1;
    ::ioctlsocket(s, FIONBIO, &on);
}
inline bool connectInProgress()
{
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
inline void closeNative(NativeSocket s) { ::close(s); }
inline void setNonBlocking(NativeSocket s)
{
    const int flags = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
}
inline bool connectInProgress() { return errno == EINPROGRESS; }
#endif

/// \brief Подождать готовности дескриптора. \return `true`, если готов; `false` — вышло время.
bool pollSocket(NativeSocket s, bool wantRead, bool wantWrite, int timeoutMs)
{
#ifdef Q_OS_WIN
    WSAPOLLFD fd;
    fd.fd = s;
    fd.events = short((wantRead ? POLLRDNORM : 0) | (wantWrite ? POLLWRNORM : 0));
    fd.revents = 0;
    return ::WSAPoll(&fd, 1, timeoutMs) > 0;
#else
    struct pollfd fd;
    fd.fd = s;
    fd.events = short((wantRead ? POLLIN : 0) | (wantWrite ? POLLOUT : 0));
    fd.revents = 0;
    return ::poll(&fd, 1, timeoutMs) > 0;
#endif
}

/// \brief Инициализация libssh2 (и Winsock) один раз на процесс.
bool initLibrary()
{
    static QMutex mutex;
    static bool done = false;
    static bool ok = false;
    QMutexLocker lock(&mutex);
    if (!done) {
        done = true;
#ifdef Q_OS_WIN
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
        ok = libssh2_init(0) == 0;
    }
    return ok;
}

/// \brief Тип ключа libssh2 → маска записи known_hosts.
int knownHostKeyMask(int hostKeyType)
{
    switch (hostKeyType) {
    case LIBSSH2_HOSTKEY_TYPE_RSA: return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    case LIBSSH2_HOSTKEY_TYPE_DSS: return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_256
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
#endif
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
    case LIBSSH2_HOSTKEY_TYPE_ED25519: return LIBSSH2_KNOWNHOST_KEY_ED25519;
#endif
    default: return 0;
    }
}

/// \brief Обратный вызов keyboard-interactive: на любой вопрос отвечает паролем.
void keyboardInteractive(const char *, int, const char *, int, int promptCount,
                         const LIBSSH2_USERAUTH_KBDINT_PROMPT *, LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses,
                         void **abstract)
{
    const auto *session = static_cast<const Libssh2Session *>(*abstract);
    const QByteArray &password = session->passwordForCallback();
    for (int i = 0; i < promptCount; ++i) {
        // Память забирает libssh2 и освобождает её сама тем же free().
        char *copy = static_cast<char *>(std::malloc(size_t(password.size()) + 1));
        if (!copy)
            return;
        std::memcpy(copy, password.constData(), size_t(password.size()));
        copy[password.size()] = '\0';
        responses[i].text = copy;
        responses[i].length = unsigned(password.size());
    }
}

/// \brief Канал libssh2, освобождаемый на любом выходе из exec().
using ChannelPtr = std::unique_ptr<LIBSSH2_CHANNEL, decltype(&libssh2_channel_free)>;

} // namespace

Libssh2Session::Libssh2Session()
{
    initLibrary();
}

Libssh2Session::~Libssh2Session()
{
    disconnectFromHost();
}

void Libssh2Session::closeSocket()
{
    if (m_socket != -1) {
        closeNative(NativeSocket(m_socket));
        m_socket = -1;
    }
}

void Libssh2Session::disconnectFromHost()
{
    if (m_session) {
        // Вежливое завершение; ждать ответа сервера незачем, мы уходим.
        libssh2_session_set_blocking(m_session, 0);
        libssh2_session_disconnect(m_session, "Spotty closing");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    closeSocket();
    m_password.clear();
}

QString Libssh2Session::lastError() const
{
    if (!m_session)
        return tr("no session");
    char *message = nullptr;
    int length = 0;
    libssh2_session_last_error(m_session, &message, &length, 0);
    return message ? QString::fromUtf8(message, length) : tr("unknown error");
}

bool Libssh2Session::waitSocket(const QDeadlineTimer &deadline)
{
    if (deadline.hasExpired())
        return false;
    const int directions = libssh2_session_block_directions(m_session);
    const bool wantRead = directions & LIBSSH2_SESSION_BLOCK_INBOUND;
    const bool wantWrite = directions & LIBSSH2_SESSION_BLOCK_OUTBOUND;
    // Короткими отрезками: срок проверяется между ними, и зависшее соединение не держит поток.
    const qint64 remaining = deadline.remainingTime();
    const int slice = int(remaining < 0 ? 500 : qMin<qint64>(remaining, 500));
    pollSocket(NativeSocket(m_socket), wantRead || !wantWrite, wantWrite, slice);
    return true;
}

template <typename Call>
int Libssh2Session::drive(Call &&call, const QDeadlineTimer &deadline)
{
    int rc;
    while ((rc = call()) == LIBSSH2_ERROR_EAGAIN) {
        if (!waitSocket(deadline))
            return LIBSSH2_ERROR_TIMEOUT;
    }
    return rc;
}

template <typename Call>
auto Libssh2Session::drivePtr(Call &&call, const QDeadlineTimer &deadline, bool *timedOut)
{
    auto result = call();
    while (!result && libssh2_session_last_errno(m_session) == LIBSSH2_ERROR_EAGAIN) {
        if (!waitSocket(deadline)) {
            *timedOut = true;
            break;
        }
        result = call();
    }
    return result;
}

bool Libssh2Session::connectToHost(const QString &host, int port, int timeoutMs, QString *error)
{
    disconnectFromHost();
    if (!initLibrary()) {
        *error = tr("libssh2 could not be initialised");
        return false;
    }
    const QDeadlineTimer deadline(timeoutMs);

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addresses = nullptr;
    const QByteArray hostUtf8 = host.toUtf8();
    const QByteArray service = QByteArray::number(port);
    if (::getaddrinfo(hostUtf8.constData(), service.constData(), &hints, &addresses) != 0 || !addresses) {
        *error = tr("cannot resolve the address");
        return false;
    }

    // Перебор адресов: у имени бывает и IPv6, и IPv4, а достижим может быть только один.
    NativeSocket connected = kInvalidSocket;
    QString lastReason = tr("connection failed");
    for (struct addrinfo *a = addresses; a && connected == kInvalidSocket; a = a->ai_next) {
        NativeSocket s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == kInvalidSocket)
            continue;
        setNonBlocking(s);
        const int rc = ::connect(s, a->ai_addr, int(a->ai_addrlen));
        bool ok = rc == 0;
        if (!ok && connectInProgress()) {
            const qint64 left = deadline.remainingTime();
            if (pollSocket(s, false, true, int(left < 0 ? 15000 : qMax<qint64>(left, 1)))) {
                int soError = 0;
#ifdef Q_OS_WIN
                int length = sizeof soError;
                ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soError), &length);
#else
                socklen_t length = sizeof soError;
                ::getsockopt(s, SOL_SOCKET, SO_ERROR, &soError, &length);
#endif
                ok = soError == 0;
                if (!ok) {
#ifdef Q_OS_WIN
                    lastReason = QString::number(soError);
#else
                    lastReason = QString::fromLocal8Bit(std::strerror(soError));
#endif
                }
            } else {
                lastReason = tr("timed out");
            }
        }
        if (ok)
            connected = s;
        else
            closeNative(s);
    }
    ::freeaddrinfo(addresses);

    if (connected == kInvalidSocket) {
        *error = lastReason;
        return false;
    }
    m_socket = std::intptr_t(connected);

    m_session = libssh2_session_init_ex(nullptr, nullptr, nullptr, this);
    if (!m_session) {
        closeSocket();
        *error = tr("cannot create an ssh session");
        return false;
    }
    libssh2_session_set_blocking(m_session, 0);

    const int rc = drive([&] { return libssh2_session_handshake(m_session, NativeSocket(m_socket)); }, deadline);
    if (rc != 0) {
        *error = rc == LIBSSH2_ERROR_TIMEOUT ? tr("the server did not answer the ssh handshake (is it an ssh server?)")
                                             : tr("ssh handshake failed: %1").arg(lastError());
        disconnectFromHost();
        return false;
    }

    // Отпечаток в том виде, в каком его показывает и сверяет ssh: хэш в base64 без добивки.
    m_fingerprint.clear();
    for (const auto &[type, name, size] : {std::tuple{LIBSSH2_HOSTKEY_HASH_SHA256, "SHA256:", 32},
                                           std::tuple{LIBSSH2_HOSTKEY_HASH_SHA1, "SHA1:", 20}}) {
        if (const char *hash = libssh2_hostkey_hash(m_session, type)) {
            m_fingerprint = QLatin1String(name)
                            + QString::fromLatin1(QByteArray(hash, size).toBase64(QByteArray::Base64Encoding
                                                                                  | QByteArray::OmitTrailingEquals));
            return true;
        }
    }
    disconnectFromHost();
    *error = tr("the server did not present a host key");
    return false;
}

SystemHostKey Libssh2Session::checkSystemKnownHosts(const QString &host, int port)
{
    if (!m_session)
        return SystemHostKey::NotFound;

    const QString path = QDir::home().filePath(QStringLiteral(".ssh/known_hosts"));
    if (!QFileInfo::exists(path))
        return SystemHostKey::NotFound;

    LIBSSH2_KNOWNHOSTS *hosts = libssh2_knownhost_init(m_session);
    if (!hosts)
        return SystemHostKey::NotFound;
    SystemHostKey verdict = SystemHostKey::NotFound;
    if (libssh2_knownhost_readfile(hosts, QFile::encodeName(path).constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH) >= 0) {
        size_t length = 0;
        int type = 0;
        const char *key = libssh2_session_hostkey(m_session, &length, &type);
        if (key) {
            const int mask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | knownHostKeyMask(type);
            struct libssh2_knownhost *entry = nullptr;
            // Порт 22 в файле записан без порта; остальные — как «[хост]:порт».
            const int rc = libssh2_knownhost_checkp(hosts, host.toUtf8().constData(), port == 22 ? -1 : port,
                                                    key, length, mask, &entry);
            if (rc == LIBSSH2_KNOWNHOST_CHECK_MATCH)
                verdict = SystemHostKey::Match;
            else if (rc == LIBSSH2_KNOWNHOST_CHECK_MISMATCH)
                verdict = SystemHostKey::Mismatch;
        }
    }
    libssh2_knownhost_free(hosts);
    return verdict;
}

bool Libssh2Session::authenticate(const SshAccess &access, QString *error)
{
    if (!m_session) {
        *error = tr("not connected");
        return false;
    }
    const QDeadlineTimer deadline(access.timeoutMs);
    const QByteArray user = access.user.toUtf8();
    m_password = access.password.toUtf8();

    // Какие способы разрешает сервер. NULL при EAGAIN — не отказ; NULL без ошибки значит,
    // что вход по имени без аутентификации уже прошёл.
    bool timedOut = false;
    char *rawList = drivePtr([&] { return libssh2_userauth_list(m_session, user.constData(), unsigned(user.size())); },
                             deadline, &timedOut);
    if (timedOut) {
        *error = tr("timed out");
        return false;
    }
    if (!rawList && libssh2_userauth_authenticated(m_session))
        return true;
    const QStringList methods = QString::fromUtf8(rawList ? rawList : "").split(QLatin1Char(','), Qt::SkipEmptyParts);

    auto fail = [&](int rc) {
        if (rc == LIBSSH2_ERROR_TIMEOUT)
            *error = tr("timed out");
        else if (rc == LIBSSH2_ERROR_AUTHENTICATION_FAILED)
            *error = tr("the server rejected the password or the key");
        else if (rc == LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED)
            *error = tr("the server does not accept this key");
        else
            *error = lastError();
        return false;
    };

    if (!access.keyFile.isEmpty()) {
        if (!methods.contains(QLatin1String("publickey"))) {
            *error = tr("the server does not allow key authentication");
            return false;
        }
        const QByteArray key = QFile::encodeName(access.keyFile);
        const QByteArray passphrase = access.keyPassphrase.toUtf8();
        const int rc = drive(
            [&] {
                return libssh2_userauth_publickey_fromfile_ex(m_session, user.constData(), unsigned(user.size()),
                                                             nullptr, key.constData(),
                                                             passphrase.isEmpty() ? nullptr : passphrase.constData());
            },
            deadline);
        if (rc != 0) {
            if (rc == LIBSSH2_ERROR_FILE) {
                *error = tr("cannot read the key file or the passphrase is wrong");
                return false;
            }
            return fail(rc);
        }
        return true;
    }

    if (!access.password.isEmpty()) {
        int rc = LIBSSH2_ERROR_AUTHENTICATION_FAILED;
        if (methods.contains(QLatin1String("password"))) {
            rc = drive(
                [&] {
                    return libssh2_userauth_password_ex(m_session, user.constData(), unsigned(user.size()),
                                                        m_password.constData(), unsigned(m_password.size()), nullptr);
                },
                deadline);
            if (rc == 0)
                return true;
        }
        // Часть серверов принимает пароль только через PAM, то есть как keyboard-interactive, хотя
        // «password» в списке стоит.
        if (methods.contains(QLatin1String("keyboard-interactive"))) {
            rc = drive(
                [&] {
                    return libssh2_userauth_keyboard_interactive_ex(m_session, user.constData(), unsigned(user.size()),
                                                                    &keyboardInteractive);
                },
                deadline);
            if (rc == 0)
                return true;
        }
        return fail(rc);
    }

    // Ни ключа, ни пароля: остаётся ssh-agent (на Windows — Pageant).
    LIBSSH2_AGENT *agent = libssh2_agent_init(m_session);
    if (!agent) {
        *error = tr("enter a password or a key file");
        return false;
    }
    bool signedIn = false;
    if (libssh2_agent_connect(agent) == 0 && libssh2_agent_list_identities(agent) == 0) {
        struct libssh2_agent_publickey *identity = nullptr;
        struct libssh2_agent_publickey *previous = nullptr;
        while (!signedIn && libssh2_agent_get_identity(agent, &identity, previous) == 0) {
            const int rc = drive([&] { return libssh2_agent_userauth(agent, user.constData(), identity); }, deadline);
            signedIn = rc == 0;
            previous = identity;
        }
        libssh2_agent_disconnect(agent);
    }
    libssh2_agent_free(agent);
    if (!signedIn)
        *error = tr("enter a password or a key file (no ssh-agent key was accepted)");
    return signedIn;
}

bool Libssh2Session::exec(const QString &command, const QByteArray &input, int timeoutMs,
                          SshExecResult *result, QString *error)
{
    if (!m_session) {
        *error = tr("not connected");
        return false;
    }
    const QDeadlineTimer deadline(timeoutMs);
    *result = {};

    bool openTimedOut = false;
    const ChannelPtr channelOwner(
        drivePtr([&] { return libssh2_channel_open_session(m_session); }, deadline, &openTimedOut),
        &libssh2_channel_free);
    LIBSSH2_CHANNEL *channel = channelOwner.get();
    if (!channel) {
        *error = openTimedOut ? tr("timed out") : tr("cannot open an ssh channel: %1").arg(lastError());
        return false;
    }

    const QByteArray commandBytes = command.toUtf8();
    int rc = drive([&] { return libssh2_channel_exec(channel, commandBytes.constData()); }, deadline);
    if (rc != 0) {
        *error = rc == LIBSSH2_ERROR_TIMEOUT ? tr("timed out") : tr("cannot run the command: %1").arg(lastError());
        return false;
    }

    // Запись и чтение идут в одном цикле: пока мы целиком заливаем вход, удалённая сторона
    // может заполнить свой выходной буфер, и без чтения обмен встал бы наглухо.
    qsizetype written = 0;
    bool eofSent = false;
    char buffer[16384];
    bool timedOut = false;
    for (;;) {
        bool progressed = false;

        if (written < input.size()) {
            const ssize_t n = libssh2_channel_write(channel, input.constData() + written, size_t(input.size() - written));
            if (n > 0) {
                written += n;
                progressed = true;
            } else if (n != LIBSSH2_ERROR_EAGAIN) {
                *error = tr("cannot send data to the command: %1").arg(lastError());
                return false;
            }
        } else if (!eofSent) {
            const int eofRc = libssh2_channel_send_eof(channel);
            if (eofRc == 0) {
                eofSent = true;
                progressed = true;
            } else if (eofRc != LIBSSH2_ERROR_EAGAIN) {
                eofSent = true; // сервер мог уже закрыть вход; читаем дальше
            }
        }

        for (const int stream : {0, SSH_EXTENDED_DATA_STDERR}) {
            for (;;) {
                const ssize_t n = stream == 0 ? libssh2_channel_read(channel, buffer, sizeof buffer)
                                              : libssh2_channel_read_stderr(channel, buffer, sizeof buffer);
                if (n > 0) {
                    (stream == 0 ? result->out : result->err).append(buffer, qsizetype(n));
                    progressed = true;
                    continue;
                }
                break;
            }
        }

        if (libssh2_channel_eof(channel) && written >= input.size())
            break;
        if (!progressed && !waitSocket(deadline)) {
            timedOut = true;
            break;
        }
        if (deadline.hasExpired()) {
            timedOut = true;
            break;
        }
    }

    if (timedOut) {
        *error = tr("timed out");
        return false;
    }

    // Код возврата известен только после закрытия канала.
    drive([&] { return libssh2_channel_close(channel); }, deadline);
    drive([&] { return libssh2_channel_wait_closed(channel); }, deadline);
    result->exitCode = libssh2_channel_get_exit_status(channel);
    return true;
}

} // namespace spotty::tcptunnel
