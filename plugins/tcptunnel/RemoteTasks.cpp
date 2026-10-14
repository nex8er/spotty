/**
 * \file RemoteTasks.cpp
 * \brief Реализация spotty::tcptunnel::RemoteTasks.
 */
#include "RemoteTasks.h"

#include "RemoteInstaller.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QThread>

#include <atomic>
#include <thread>

namespace spotty::tcptunnel {

namespace {

/// \brief Отвечает ли порт агента с этой стороны: не фиксирует работу UART, только доступность по сети.
bool portAnswers(const QString &host, int port, int timeoutMs)
{
    const QDeadlineTimer deadline(timeoutMs);
    // Агенту нужна секунда на запуск, а под systemd он ещё и перезапускается: пробуем, пока не вышел срок.
    while (!deadline.hasExpired()) {
        QTcpSocket socket;
        socket.connectToHost(host, quint16(port));
        if (socket.waitForConnected(1000))
            return true;
        QThread::msleep(300);
    }
    return false;
}

} // namespace

struct RemoteTasks::State
{
    mutable QMutex mutex;
    bool running = true;
    bool ok = false;
    Kind kind = Kind::FindPorts;
    QString text;
    bool hasHost = false;
    HostInfo host;
    QList<UartPort> ports;
    std::atomic<bool> cancelled{false};

    /// \brief Ключ, от которого отказались как от неизвестного: повтор действия его подтвердит.
    QString pendingHost;
    int pendingPort = 0;
    QString pendingFingerprint;
};

RemoteTasks::RemoteTasks(std::shared_ptr<KnownHosts> knownHosts, std::shared_ptr<AgentPackage> package,
                         SessionFactory factory)
    : m_knownHosts(std::move(knownHosts))
    , m_package(std::move(package))
    , m_factory(std::move(factory))
{
}

RemoteTasks::~RemoteTasks()
{
    // Только просим: см. описание класса.
    QMutexLocker lock(&m_mutex);
    for (const auto &state : std::as_const(m_states))
        state->cancelled = true;
}

QString RemoteTasks::start(const QString &deviceId, Kind kind, const SshAccess &requested,
                           const InstallParams &params)
{
    SshAccess access = requested;
    auto state = std::make_shared<State>();
    state->kind = kind;
    {
        QMutexLocker lock(&m_mutex);
        const auto previous = m_states.value(deviceId);
        if (previous) {
            QMutexLocker previousLock(&previous->mutex);
            if (previous->running)
                return tr("The previous operation on this connection is still running.");
            // Найденное прошлой операцией (порты, сведения о машине) переживает следующую: после
            // установки список портов не должен пропадать из поля.
            state->hasHost = previous->hasHost;
            state->host = previous->host;
            state->ports = previous->ports;

            // Прошлая попытка отказалась от неизвестного ключа и показала его отпечаток. Повтор
            // действия на тот же хост — подтверждение: принимается именно этот ключ и никакой другой.
            if (previous->pendingHost == access.host && previous->pendingPort == access.port
                && access.trustFingerprint.isEmpty()) {
                access.trustFingerprint = previous->pendingFingerprint;
            }
        }
        m_states.insert(deviceId, state);
    }

    // Всё, что нужно потоку, — по значению или через shared_ptr: сам RemoteTasks к его концу может не дожить.
    auto knownHosts = m_knownHosts;
    auto package = m_package;
    auto factory = m_factory;
    std::thread([state, kind, access, params, knownHosts, package, factory] {
        auto setText = [&state](const QString &text) {
            QMutexLocker lock(&state->mutex);
            state->text = text;
        };
        auto finish = [&state](bool ok, const QString &text) {
            QMutexLocker lock(&state->mutex);
            state->running = false;
            state->ok = ok;
            state->text = text;
        };

        std::unique_ptr<ISshSession> session = factory();
        RemoteInstaller installer(*session, *package, *knownHosts,
                                  {setText, [&state] { return state->cancelled.load(); }});

        QString error;
        HostInfo info;
        if (!installer.connect(access, &error) || !installer.probe(&info, &error)) {
            session->disconnectFromHost();
            if (!installer.refusedFingerprint().isEmpty()) {
                QMutexLocker lock(&state->mutex);
                state->pendingHost = access.host;
                state->pendingPort = access.port;
                state->pendingFingerprint = installer.refusedFingerprint();
            }
            finish(false, error);
            return;
        }
        {
            QMutexLocker lock(&state->mutex);
            state->host = info;
            state->hasHost = true;
        }

        bool ok = false;
        QString done;
        switch (kind) {
        case Kind::FindPorts: {
            QList<UartPort> ports;
            ok = installer.scanUarts(info, &ports, &error);
            if (ok) {
                {
                    QMutexLocker lock(&state->mutex);
                    state->ports = ports;
                }
                done = RemoteTasks::tr("%1. Serial ports found: %2. First free port for an agent: %3.")
                           .arg(info.summary())
                           .arg(ports.size())
                           .arg(nextFreePort(info, 7777));
            }
            break;
        }
        case Kind::Install: {
            ok = installer.install(info, params, &error);
            if (ok) {
                setText(RemoteTasks::tr("Checking that the agent answers..."));
                if (portAnswers(access.host, params.port, 8000)) {
                    done = RemoteTasks::tr("Installed: the agent listens on %1:%2 (%3, %4 %5). You can open this connection now.")
                               .arg(access.host)
                               .arg(params.port)
                               .arg(params.device)
                               .arg(params.baud)
                               .arg(params.format.toUpper());
                } else {
                    // Установлено, но по сети не видно: файрвол, другая сеть, привязка к 127.0.0.1.
                    done = RemoteTasks::tr("The agent was installed and started, but port %1 of %2 does not answer from here. "
                              "Check the firewall of the machine, the listen address and the network.")
                               .arg(params.port)
                               .arg(access.host);
                }
            }
            break;
        }
        case Kind::Uninstall:
            ok = installer.uninstall(info, params.port, &error);
            if (ok) {
                done = RemoteTasks::tr("The agent on port %1 was removed from the machine. You can remove this connection now.")
                           .arg(params.port);
            }
            break;
        }

        session->disconnectFromHost();
        finish(ok, ok ? done : error);
    }).detach();
    return {};
}

RemoteTasks::Snapshot RemoteTasks::snapshot(const QString &deviceId) const
{
    std::shared_ptr<State> state;
    {
        QMutexLocker lock(&m_mutex);
        state = m_states.value(deviceId);
    }
    Snapshot snapshot;
    if (!state)
        return snapshot;

    QMutexLocker lock(&state->mutex);
    snapshot.known = true;
    snapshot.running = state->running;
    snapshot.ok = state->ok;
    snapshot.kind = state->kind;
    snapshot.text = state->text;
    snapshot.hasHost = state->hasHost;
    snapshot.host = state->host;
    snapshot.ports = state->ports;
    return snapshot;
}

bool RemoteTasks::waitForIdle(int timeoutMs) const
{
    const QDeadlineTimer deadline(timeoutMs);
    for (;;) {
        bool busy = false;
        {
            QMutexLocker lock(&m_mutex);
            for (const auto &state : m_states) {
                QMutexLocker stateLock(&state->mutex);
                busy = busy || state->running;
            }
        }
        if (!busy)
            return true;
        if (deadline.hasExpired())
            return false;
        QThread::msleep(20);
    }
}

} // namespace spotty::tcptunnel
