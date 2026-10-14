/**
 * \file NetworkScanner.cpp
 * \brief Реализация spotty::NetworkScanner.
 */
#include "NetworkScanner.h"

#include <QHostInfo>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>

namespace spotty {

namespace {

/// \brief Сеть по адресу и длине префикса: адрес с обнулёнными хостовыми битами.
quint32 networkOf(quint32 address, int prefix)
{
    const quint32 mask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
    return address & mask;
}

/// \brief Добавить хосты сети в список, не повторяя уже добавленные.
void appendNetwork(QList<QHostAddress> *out, QSet<quint32> *seen, quint32 address, int prefix)
{
    const quint32 network = networkOf(address, prefix);
    const quint32 hostCount = prefix >= 32 ? 1u : (1u << (32 - prefix));

    // Для /31 и /32 адресов-«сети» и «широковещательного» нет: все адреса — хосты. Для
    // остальных первый и последний на хосты не отвечают.
    const bool pointToPoint = prefix >= 31;
    const quint32 first = pointToPoint ? network : network + 1;
    const quint32 last = pointToPoint ? network + hostCount - 1 : network + hostCount - 2;

    // Верхняя граница в 64-битном счётчике: на 255.255.255.255 цикл на quint32 не завершился бы.
    for (quint64 value = first; value <= last; ++value) {
        const quint32 v = quint32(value);
        if (seen->contains(v))
            continue;
        seen->insert(v);
        out->append(QHostAddress(v));
    }
}

} // namespace

NetworkScanner::NetworkScanner(QObject *parent)
    : QObject(parent)
{
}

NetworkScanner::~NetworkScanner()
{
    cancel();
}

bool NetworkScanner::parseRange(const QString &text, QList<QHostAddress> *targets, QString *error)
{
    targets->clear();
    QSet<quint32> seen;

    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                          Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(token);
        if (subnet.first.isNull() || subnet.first.protocol() != QAbstractSocket::IPv4Protocol) {
            if (error)
                *error = tr("\"%1\" is not an IPv4 address or network (expected like 192.168.1.0/24).")
                             .arg(token);
            targets->clear();
            return false;
        }

        // /20 — это 4094 хоста, как раз потолок. Отсекается до перебора, а не после: /8
        // успел бы выделить шестнадцать миллионов адресов, прежде чем счётчик это заметил.
        const int prefix = subnet.second;
        if (prefix < 20) {
            if (error)
                *error = tr("The range \"%1\" is too large (at most %2 addresses).")
                             .arg(token)
                             .arg(kMaxTargets);
            targets->clear();
            return false;
        }

        appendNetwork(targets, &seen, subnet.first.toIPv4Address(), prefix);
        if (targets->size() > kMaxTargets) {
            if (error)
                *error = tr("The range is too large (more than %1 addresses).").arg(kMaxTargets);
            targets->clear();
            return false;
        }
    }
    return true;
}

QList<NetworkScanner::LocalNetwork> NetworkScanner::localNetworks()
{
    QList<LocalNetwork> result;
    QSet<QString> seen;

    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack)
            || (flags & QNetworkInterface::IsPointToPoint)) {
            continue;
        }

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            int prefix = entry.prefixLength();
            if (prefix < 22)
                prefix = 24; // см. описание в заголовке
            if (prefix > 30)
                continue; // сеть из двух адресов: опрашивать там нечего

            const quint32 network = networkOf(entry.ip().toIPv4Address(), prefix);
            const QString cidr = QStringLiteral("%1/%2").arg(QHostAddress(network).toString()).arg(prefix);
            if (seen.contains(cidr))
                continue; // одна сеть через несколько интерфейсов (Wi-Fi и кабель)
            seen.insert(cidr);
            result.append({cidr, iface.name()});
        }
    }
    return result;
}

QList<QHostAddress> NetworkScanner::localTargets()
{
    QList<QHostAddress> result;
    QSet<quint32> seen;
    for (const LocalNetwork &network : localNetworks()) {
        const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(network.cidr);
        appendNetwork(&result, &seen, subnet.first.toIPv4Address(), subnet.second);
    }
    return result;
}

void NetworkScanner::start(const Options &options)
{
    cancel();

    m_options = options;
    m_hosts.clear();
    m_candidates.clear();
    m_controlsPending.clear();
    m_intercepted.clear();
    m_queue.clear();

    // Контрольные порты: заведомо ничьи. Не должны попадать в диапазон самого поиска, иначе
    // настоящий агент был бы принят за перехват.
    m_controlPorts.clear();
    if (options.verifyHosts) {
        QList<int> candidates = options.controlPorts;
        if (candidates.isEmpty()) {
            const int far = options.firstPort + 1000 <= 65535 ? options.firstPort + 1000 : options.firstPort - 1000;
            candidates = {1, far};
        }
        for (int port : std::as_const(candidates)) {
            const bool insideSearch = port >= options.firstPort && port < options.firstPort + options.maxPorts;
            if (port >= 1 && port <= 65535 && !insideSearch && !m_controlPorts.contains(port))
                m_controlPorts.append(port);
        }
    }
    m_hostsDone = 0;
    m_targetCount = int(options.targets.size());
    m_running = true;
    ++m_generation;

    for (const QHostAddress &address : options.targets)
        m_queue.push_back({address, options.firstPort, false});

    Q_EMIT changed();
    if (m_queue.empty()) {
        finishIfIdle();
        return;
    }
    pump();
}

void NetworkScanner::cancel()
{
    ++m_generation; // запоздалые сигналы уже отключённых сокетов ничего не изменят
    m_queue.clear();

    const QList<QTcpSocket *> sockets = m_sockets;
    m_sockets.clear();
    for (QTcpSocket *socket : sockets) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }

    if (m_running) {
        m_running = false;
        Q_EMIT changed();
        Q_EMIT finished();
    }
}

QList<NetworkScanner::Host> NetworkScanner::results() const
{
    QList<Host> result = m_hosts.values();
    std::sort(result.begin(), result.end(), [](const Host &a, const Host &b) {
        return a.address.toIPv4Address() < b.address.toIPv4Address();
    });
    return result;
}

void NetworkScanner::pump()
{
    while (int(m_sockets.size()) < m_options.concurrency && !m_queue.empty()) {
        const Probe probe = m_queue.front();
        m_queue.pop_front();
        launch(probe);
    }
}

void NetworkScanner::launch(const Probe &probe)
{
    auto *socket = new QTcpSocket(this);
    m_sockets.append(socket);
    const int generation = m_generation;

    // Любой из трёх исходов — соединились, отказали, вышло время — завершает пробу ровно
    // один раз: complete() сам себя защищает, убирая сокет из m_sockets при первом вызове.
    connect(socket, &QTcpSocket::connected, this,
            [this, socket, probe, generation] { complete(socket, probe, true, generation); });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, socket, probe, generation] { complete(socket, probe, false, generation); });

    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this,
            [this, socket, probe, generation] { complete(socket, probe, false, generation); });
    timer->start(m_options.timeoutMs);

    socket->connectToHost(probe.address, quint16(probe.port));
}

void NetworkScanner::complete(QTcpSocket *socket, const Probe &probe, bool open, int generation)
{
    if (generation != m_generation || !m_sockets.contains(socket))
        return;

    m_sockets.removeOne(socket);
    socket->disconnect(this);
    socket->abort();
    // Не delete: complete() зовётся из сигнала самого сокета.
    socket->deleteLater();

    const QString key = probe.address.toString();

    if (probe.control) {
        // Ответ на контрольную пробу: открыт хоть один заведомо ничей порт — хост принимает
        // соединения на всё подряд, и «открытый» первый порт ничего о нём не говорит.
        const int left = --m_controlsPending[key];
        if (open) {
            m_intercepted.insert(key);
            m_candidates.remove(key);
        } else if (left == 0 && m_candidates.contains(key)) {
            confirmHost(key);
        }
        Q_EMIT changed();
        pump();
        finishIfIdle();
        return;
    }

    if (probe.port == m_options.firstPort)
        ++m_hostsDone;

    if (open && !m_intercepted.contains(key)) {
        if (probe.port == m_options.firstPort) {
            Host host;
            host.address = probe.address;
            host.ports = {probe.port};
            m_candidates.insert(key, host);
            if (m_controlPorts.isEmpty()) {
                confirmHost(key); // проверка отключена или нечем проверять
            } else {
                m_controlsPending[key] = int(m_controlPorts.size());
                // Вне очереди: хост нужно решить, пока на него не потрачено ничего другого.
                for (int control : std::as_const(m_controlPorts))
                    m_queue.push_front({probe.address, control, true});
            }
        } else if (m_hosts.contains(key)) {
            m_hosts[key].ports.append(probe.port);
            queueNextPort(probe.address, probe.port);
        }
    }

    Q_EMIT changed();
    pump();
    finishIfIdle();
}

void NetworkScanner::confirmHost(const QString &key)
{
    Host host = m_candidates.take(key);
    m_hosts.insert(key, host);

    // Следующий порт — вне очереди: у только что найденного хоста он самый вероятный, и
    // порядок «все первые, потом вторые» заставил бы ждать результат до конца прохода.
    queueNextPort(host.address, host.ports.first());

    if (m_options.resolveNames) {
        const int resolving = m_generation;
        QHostInfo::lookupHost(key, this, [this, key, resolving](const QHostInfo &info) {
            if (resolving != m_generation)
                return; // результат уже сброшенного прохода
            const auto it = m_hosts.find(key);
            if (it == m_hosts.end() || info.error() != QHostInfo::NoError)
                return;
            const QString name = info.hostName();
            // Обратный поиск без записи возвращает сам адрес — это не имя.
            if (!name.isEmpty() && name != key && it->name != name) {
                it->name = name;
                Q_EMIT changed();
            }
        });
    }
}

void NetworkScanner::queueNextPort(const QHostAddress &address, int port)
{
    if (port + 1 < m_options.firstPort + m_options.maxPorts && port < 65535)
        m_queue.push_front({address, port + 1, false});
}

void NetworkScanner::finishIfIdle()
{
    if (!m_running || !m_queue.empty() || !m_sockets.isEmpty())
        return;
    m_running = false;
    Q_EMIT changed();
    Q_EMIT finished();
}

} // namespace spotty
