/**
 * \file TcpTunnelPlugin.cpp
 * \brief Реализация spotty::TcpTunnelPlugin.
 */
#include "TcpTunnelPlugin.h"

#include "RemoteHost.h"
#include "TcpTunnelChannel.h"
#include "TcpTunnelEndpoint.h"
#include "TcpTunnelSettings.h"

#ifdef SPOTTY_HAVE_LIBSSH2
#include "Libssh2Session.h"
#endif

#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>

namespace spotty {

TcpTunnelPlugin::TcpTunnelPlugin()
{
    // Сканер находит имена обратным DNS уже после того, как адрес показан; подпись подключения
    // должна подхватить их без участия пользователя.
    connect(&m_scanner, &NetworkScanner::changed, this, [this] { learnScannedNames(); });

#ifdef SPOTTY_HAVE_LIBSSH2
    m_tasks = std::make_unique<tcptunnel::RemoteTasks>(
        m_knownHosts, tcptunnel::resourceAgentPackage(),
        [] { return std::unique_ptr<tcptunnel::ISshSession>(new tcptunnel::Libssh2Session); });
#endif
}

TcpTunnelPlugin::~TcpTunnelPlugin() = default;

void TcpTunnelPlugin::setDataDirectory(const QString &path)
{
    // Каталог создаётся при первой записи: до тех пор хранить нечего.
    m_knownHosts->setFile(QDir(path).filePath(QStringLiteral("known_hosts")));
    m_hostNames->setFile(QDir(path).filePath(QStringLiteral("host_names")));
}

void TcpTunnelPlugin::setSshForTesting(tcptunnel::RemoteTasks::SessionFactory factory,
                                       std::shared_ptr<tcptunnel::AgentPackage> package)
{
    m_tasks = std::make_unique<tcptunnel::RemoteTasks>(m_knownHosts, std::move(package), std::move(factory));
}

void TcpTunnelPlugin::addInstallerFields(SettingsSchema &schema) const
{
    using namespace tcptunnel;
    const QString agent = tr("Agent on the machine");

    // Вход по ssh здесь не настраивается: имя и пароль спрашиваются окном перед действием
    // (promptSchema()) и нигде не хранятся.
    //
    // Кнопка поиска стоит справа от списка, который она заполняет: «где взять порт» и «какой порт»
    // — один жест, а не кнопка над полем и поле под ней.
    schema.add(SettingsField{
        .key = QLatin1String(kRemoteUart),
        .label = tr("Serial port"),
        .group = agent,
        .type = SettingsField::Choice,
        .defaultValue = QString(),
        .live = true,
        .editable = true,
        .hint = tr("Virtual consoles (tty1...) and ports without hardware are not listed."),
    });
    schema.add(SettingsField{
        .key = QLatin1String(kFindPortsAction),
        .label = tr("Find ports..."),
        .group = agent,
        .type = SettingsField::Action,
        .hint = tr("Sign in to the machine over SSH and list its serial ports."),
        .compact = true,
    });

    QList<SettingsOption> bauds;
    for (int baud : supportedBauds())
        bauds.append({QString::number(baud), baud});
    schema.add(SettingsField{
        .key = QLatin1String(kBaud),
        .label = tr("Baud rate"),
        .group = agent,
        .type = SettingsField::Choice,
        .defaultValue = 115200,
        .options = bauds,
        .editable = true,
        .suffix = tr("bps"),
        .hint = tr("Applied when the agent is installed; to change it, install again."),
    });
    schema.add(SettingsField{
        .key = QLatin1String(kFormat),
        .label = tr("Format"),
        .group = agent,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("8N1"),
        .options = {{QStringLiteral("8N1"), QStringLiteral("8N1")},
                    {QStringLiteral("8E1"), QStringLiteral("8E1")},
                    {QStringLiteral("8O1"), QStringLiteral("8O1")},
                    {QStringLiteral("8N2"), QStringLiteral("8N2")},
                    {QStringLiteral("7E1"), QStringLiteral("7E1")},
                    {QStringLiteral("7O1"), QStringLiteral("7O1")}},
        .hint = tr("Data bits, parity and stop bits."),
        .compact = true,
    });
    schema.add(SettingsField{
        .key = QLatin1String(kAutostart),
        .label = tr("Start at boot"),
        .group = agent,
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("Installs a systemd service."),
    });
    schema.add(SettingsField{
        .key = QLatin1String(kFlow),
        .label = tr("Flow control"),
        .group = agent,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("none"),
        .options = {{tr("None"), QStringLiteral("none")},
                    {tr("Hardware (RTS/CTS)"), QStringLiteral("rtscts")},
                    {tr("Software (XON/XOFF)"), QStringLiteral("xonxoff")}},
        .advanced = true,
    });
    schema.add(SettingsField{
        .key = QLatin1String(kBind),
        .label = tr("Listen on"),
        .group = agent,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("0.0.0.0"),
        .options = {{tr("All interfaces"), QStringLiteral("0.0.0.0")},
                    {tr("All interfaces, including IPv6"), QStringLiteral("::")},
                    {tr("This machine only"), QStringLiteral("127.0.0.1")}},
        .editable = true,
        .advanced = true,
    });
    schema.add(SettingsField{
        .key = QLatin1String(kAllow),
        .label = tr("Allowed clients"),
        .group = agent,
        .type = SettingsField::Text,
        .defaultValue = QString(),
        .hint = tr("Optional protection: addresses and networks, comma separated (192.168.1.0/24, "
                   "10.0.0.5). Empty - anyone who can reach the port. The connection has no "
                   "password, so this list is the only protection."),
        .advanced = true,
    });
    // Забыть отпечаток нужно раз в жизни машины (переустановили систему), поэтому не на виду.
    schema.add(SettingsField{
        .key = QLatin1String(kForgetKeyAction),
        .label = tr("Forget the saved SSH host key"),
        .group = agent,
        .type = SettingsField::Action,
        .hint = tr("Spotty remembers each machine's SSH key. Use this after the machine's system "
                   "was reinstalled."),
        .advanced = true,
    });

    // Главное действие и опасное — в одном ряду, но различимы: у первого акцентный контур, у второго
    // название «удалить» без двусмысленности (кнопка «Remove» вверху диалога убирает запись, а не агента).
    schema.add(SettingsField{
        .key = QLatin1String(kInstallAction),
        .label = tr("Install or update the agent..."),
        .group = agent,
        .type = SettingsField::Action,
        .primary = true,
    });
    schema.add(SettingsField{
        .key = QLatin1String(kUninstallAction),
        .label = tr("Uninstall the agent..."),
        .group = agent,
        .type = SettingsField::Action,
        .compact = true,
    });
    schema.add(SettingsField{
        .key = QLatin1String(kRemoteStatus),
        .group = agent,
        .type = SettingsField::Note,
        .defaultValue = tr("Press \"Find ports...\": Spotty will ask for the SSH user name and "
                           "password."),
        .live = true,
    });
}

SettingsSchema TcpTunnelPlugin::settingsSchema() const
{
    using namespace tcptunnel;
    SettingsSchema schema;

    // Адрес и порт — редактируемые списки с живыми пунктами: сюда сканер выкладывает найденное,
    // но набрать можно и своё. Выбор из найденного — единственный способ «подставить» результат
    // поиска в форму, у которой нет ни связей между полями, ни собственного окна. Порт узкий и
    // стоит справа от адреса.
    schema.add(SettingsField{
        .key = QLatin1String(kHost),
        .label = tr("Host"),
        .group = tr("Connection"),
        .type = SettingsField::Choice,
        .defaultValue = QString(),
        .live = true,
        .editable = true,
        .required = true,
        .hint = tr("IP address or name of the machine where the agent runs. Hosts found by the "
                   "scan are offered in the list."),
    });

    schema.add(SettingsField{
        .key = QLatin1String(kPort),
        .label = tr("Port"),
        .group = tr("Connection"),
        .type = SettingsField::Choice,
        .defaultValue = kDefaultPort,
        .options = {{QString::number(kDefaultPort), kDefaultPort}},
        .live = true,
        .editable = true,
        .hint = tr("The first agent on a machine listens on 7777, the next ones on 7778, "
                   "7779 and so on - one agent serves one serial port."),
        .compact = true,
    });

    schema.add(SettingsField{
        .key = QLatin1String(kAutoReconnect),
        .label = tr("Reconnect automatically"),
        .group = tr("Connection"),
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("Keep trying when the connection drops: the board rebooted, the network "
                   "blinked or the agent was restarted."),
        .advanced = true,
    });

    schema.add(SettingsField{
        .key = QLatin1String(kConnectTimeoutMs),
        .label = tr("Connection timeout"),
        .group = tr("Connection"),
        .type = SettingsField::Integer,
        .defaultValue = 3000,
        .minimum = 200,
        .maximum = 60000,
        .suffix = tr("ms"),
        .advanced = true,
    });

    // Диапазон выбирается из сетей этого компьютера (живые пункты), но можно набрать и свой. Кнопка
    // запуска — отдельной строкой под ним, а не третьей в ряду: диалог бывает всего 520 px в ширину
    // (страница общих настроек), и диапазон рядом с портом и кнопкой сжался бы до нечитаемого.
    schema.add(SettingsField{
        .key = QLatin1String(kScanRange),
        .label = tr("Network"),
        .group = tr("Find on the network"),
        .type = SettingsField::Choice,
        .defaultValue = QString(),
        .options = {{tr("All networks of this computer"), QString()}},
        .live = true,
        .editable = true,
        .hint = tr("A network of this computer, or addresses you type: 192.168.1.0/24, 10.0.0.5"),
    });

    schema.add(SettingsField{
        .key = QLatin1String(kScanPort),
        .label = tr("Port"),
        .group = tr("Find on the network"),
        .type = SettingsField::Integer,
        .defaultValue = kDefaultPort,
        .minimum = 1,
        .maximum = 65535,
        .hint = tr("Where an open port is found, the next three are checked as well: that is "
                   "where further agents of the same machine listen."),
        .compact = true,
    });

    schema.add(SettingsField{
        .key = QLatin1String(kScanAction),
        .label = tr("Scan network"),
        .group = tr("Find on the network"),
        .type = SettingsField::Action,
        .primary = true,
    });

    schema.add(SettingsField{
        .key = QLatin1String(kScanStatus),
        .group = tr("Find on the network"),
        .type = SettingsField::Note,
        .defaultValue = tr("Not scanned yet."),
        .live = true,
    });

    if (sshAvailable())
        addInstallerFields(schema);

    return schema;
}

SettingsSchema TcpTunnelPlugin::promptSchema(const QString &actionKey, const QVariantMap &settings) const
{
    using namespace tcptunnel;
    QString what;
    if (actionKey == QLatin1String(kFindPortsAction))
        what = tr("look at the machine and list its serial ports");
    else if (actionKey == QLatin1String(kInstallAction))
        what = tr("install the agent");
    else if (actionKey == QLatin1String(kUninstallAction))
        what = tr("remove the agent");
    else
        return {};

    const QString host = settings.value(QLatin1String(kHost)).toString().trimmed();
    const QString group = tr("SSH access to %1").arg(host.isEmpty() ? tr("the machine") : host);

    SettingsSchema schema;
    schema.add(SettingsField{
        .key = QStringLiteral("note"),
        .group = group,
        .type = SettingsField::Note,
        .defaultValue = tr("Spotty signs in over SSH to %1. The user needs root rights or sudo. "
                           "Nothing entered here is saved.")
                            .arg(what),
    });
    schema.add(SettingsField{
        .key = QLatin1String(kSshUser),
        .label = tr("User name"),
        .group = group,
        .type = SettingsField::Text,
        .defaultValue = QString(),
        .required = true,
    });
    schema.add(SettingsField{
        .key = QLatin1String(kSshPassword),
        .label = tr("Password"),
        .group = group,
        .type = SettingsField::Password,
        .hint = tr("Also used for sudo. Leave empty to sign in through ssh-agent."),
    });
    schema.add(SettingsField{
        .key = QLatin1String(kSshKeyFile),
        .label = tr("Key file"),
        .group = group,
        .type = SettingsField::Text,
        .defaultValue = QString(),
        .hint = tr("Optional: path to a private key (for example ~/.ssh/id_ed25519). The password "
                   "is then used for sudo only."),
        .advanced = true,
    });
    schema.add(SettingsField{
        .key = QLatin1String(kSshKeyPassphrase),
        .label = tr("Key passphrase"),
        .group = group,
        .type = SettingsField::Password,
        .advanced = true,
    });
    schema.add(SettingsField{
        .key = QLatin1String(kSshPort),
        .label = tr("SSH port"),
        .group = group,
        .type = SettingsField::Integer,
        .defaultValue = 22,
        .minimum = 1,
        .maximum = 65535,
        .advanced = true,
    });
    return schema;
}

IInterfaceChannel *TcpTunnelPlugin::createChannel(const InterfaceDescriptor &descriptor)
{
    Q_UNUSED(descriptor); // адрес приходит в open() вместе с настройками
    return new TcpTunnelChannel;
}

InterfaceDescriptor TcpTunnelPlugin::describeUserDevice(const QVariantMap &settings) const
{
    return tcptunnel::describeEndpoint(settings, tr("New connection"), m_hostNames.get());
}

QString TcpTunnelPlugin::settingsSummary(const QVariantMap &settings) const
{
    return tcptunnel::uartSummary(settings);
}

QString TcpTunnelPlugin::triggerAction(const InterfaceDescriptor &descriptor, const QString &key,
                                       const QVariantMap &settings)
{
    using namespace tcptunnel;
    if (key == QLatin1String(kScanAction)) {
        NetworkScanner::Options options;
        options.firstPort =
            qBound(1, settings.value(QLatin1String(kScanPort), kDefaultPort).toInt(), 65535);

        const QString range = settings.value(QLatin1String(kScanRange)).toString().trimmed();
        if (range.isEmpty()) {
            options.targets = NetworkScanner::localTargets();
            if (options.targets.isEmpty()) {
                return tr("No active IPv4 network was found on this computer. Enter a range to scan, "
                          "for example 192.168.1.0/24.");
            }
            m_scanScope = tr("local networks");
        } else {
            QString error;
            if (!NetworkScanner::parseRange(range, &options.targets, &error))
                return error;
            m_scanScope = range;
        }

        m_scanner.start(options);
        return {};
    }
    return triggerRemoteAction(descriptor, key, settings);
}

QString TcpTunnelPlugin::triggerRemoteAction(const InterfaceDescriptor &descriptor, const QString &key,
                                             const QVariantMap &settings)
{
    using namespace tcptunnel;
    const bool isRemote = key == QLatin1String(kForgetKeyAction) || key == QLatin1String(kFindPortsAction)
                          || key == QLatin1String(kInstallAction) || key == QLatin1String(kUninstallAction);
    if (!isRemote)
        return {};
    if (!m_tasks) {
        return tr("This build of Spotty has no ssh support (libssh2 was not available when it was "
                  "built), so the agent cannot be installed from here.");
    }

    SshAccess access;
    access.host = settings.value(QLatin1String(kHost)).toString().trimmed();
    access.port = settings.value(QLatin1String(kSshPort), 22).toInt();
    access.user = settings.value(QLatin1String(kSshUser)).toString().trimmed();
    access.password = settings.value(QLatin1String(kSshPassword)).toString();
    access.keyFile = settings.value(QLatin1String(kSshKeyFile)).toString().trimmed();
    access.keyPassphrase = settings.value(QLatin1String(kSshKeyPassphrase)).toString();

    if (access.host.isEmpty())
        return tr("Enter the address of the machine first (the Host field).");

    if (key == QLatin1String(kForgetKeyAction)) {
        return m_knownHosts->forget(access.host, access.port)
                   ? QString()
                   : tr("Spotty has no saved ssh key for %1:%2.").arg(access.host).arg(access.port);
    }

    if (access.user.isEmpty())
        return tr("Enter the ssh user name.");

    InstallParams params;
    params.port = settings.value(QLatin1String(kPort), kDefaultPort).toInt();
    tcptunnel::RemoteTasks::Kind kind = tcptunnel::RemoteTasks::Kind::FindPorts;
    if (key == QLatin1String(kInstallAction)) {
        kind = tcptunnel::RemoteTasks::Kind::Install;
        params.device = settings.value(QLatin1String(kRemoteUart)).toString().trimmed();
        params.baud = settings.value(QLatin1String(kBaud), 115200).toInt();
        params.format = settings.value(QLatin1String(kFormat), QStringLiteral("8N1")).toString();
        params.flow = settings.value(QLatin1String(kFlow), QStringLiteral("none")).toString();
        params.bind = settings.value(QLatin1String(kBind), QStringLiteral("0.0.0.0")).toString().trimmed();
        params.allow = settings.value(QLatin1String(kAllow)).toString();
        params.autostart = settings.value(QLatin1String(kAutostart), true).toBool();
        // Ошибки в параметрах видны сразу окном, а не после подключения по ssh.
        const QString invalid = validateInstallParams(params);
        if (!invalid.isEmpty()) {
            return params.device.isEmpty()
                       ? tr("Choose the serial port first: press \"Find ports...\" and pick one.")
                       : invalid;
        }
    } else if (key == QLatin1String(kUninstallAction)) {
        kind = tcptunnel::RemoteTasks::Kind::Uninstall;
    }

    return m_tasks->start(descriptor.id, kind, access, params);
}

QList<SettingsOption> TcpTunnelPlugin::liveOptions(const InterfaceDescriptor &descriptor,
                                                   const QString &key, const QVariantMap &settings)
{
    using namespace tcptunnel;

    if (key == QLatin1String(kScanStatus)) {
        NoteSeverity severity = NoteSeverity::Neutral;
        const QString text = scanStatusText(&severity);
        return text.isEmpty() ? QList<SettingsOption>{}
                              : QList<SettingsOption>{{text, noteSeverityValue(severity)}};
    }

    const tcptunnel::RemoteTasks::Snapshot remote =
        m_tasks ? m_tasks->snapshot(descriptor.id) : tcptunnel::RemoteTasks::Snapshot{};

    // Имя машины пришло по ssh: из него составляется подпись подключения. Узнаётся здесь, в потоке
    // UI, а не в рабочем потоке — тот к плагину не обращается.
    if (remote.hasHost && !remote.host.hostname.isEmpty())
        learnName(settings.value(QLatin1String(kHost)).toString(), remote.host.hostname);

    // Адрес набран руками, поиска и ssh не было: имя можно узнать обратным DNS, и подпись станет
    // «имя:порт», а не «адрес:порт». Один запрос на адрес за сеанс — опрос идёт раз в секунду.
    const QString typedHost = settings.value(QLatin1String(kHost)).toString().trimmed();
    if (!typedHost.isEmpty() && !QHostAddress(typedHost).isNull() && m_hostNames->nameFor(typedHost).isEmpty()
        && !m_nameLookups.contains(typedHost)) {
        m_nameLookups.insert(typedHost);
        QHostInfo::lookupHost(typedHost, this, [this, typedHost](const QHostInfo &info) {
            if (info.error() == QHostInfo::NoError)
                learnName(typedHost, info.hostName());
        });
    }

    if (key == QLatin1String(kScanRange)) {
        QList<SettingsOption> networks;
        for (const NetworkScanner::LocalNetwork &network : NetworkScanner::localNetworks())
            networks.append({QStringLiteral("%1  (%2)").arg(network.cidr, network.interfaceName), network.cidr});
        return networks;
    }

    if (key == QLatin1String(kRemoteStatus)) {
        if (!remote.known || remote.text.isEmpty())
            return {};
        // Пока операция идёт, текст — ход работы; после неё — итог, и подаётся как успех или сбой.
        const NoteSeverity severity = remote.running ? NoteSeverity::Neutral
                                                     : (remote.ok ? NoteSeverity::Ok : NoteSeverity::Error);
        return {{remote.text, noteSeverityValue(severity)}};
    }

    if (key == QLatin1String(kRemoteUart)) {
        QList<SettingsOption> options;
        for (const UartPort &port : remote.ports) {
            const QString name = QFileInfo(port.device).fileName();
            options.append({port.description.isEmpty() ? name
                                                       : QStringLiteral("%1  -  %2").arg(name, port.description),
                            port.device});
        }
        return options;
    }

    QList<SettingsOption> options;
    const QList<NetworkScanner::Host> hosts = m_scanner.results();

    if (key == QLatin1String(kHost)) {
        for (const NetworkScanner::Host &host : hosts) {
            QStringList ports;
            for (int port : host.ports)
                ports << QString::number(port);
            const QString address = host.address.toString();
            const QString name = host.name.isEmpty() ? address
                                                     : QStringLiteral("%1 (%2)").arg(address, host.name);
            options.append({QStringLiteral("%1  ·  %2").arg(name, ports.join(QStringLiteral(", "))),
                            address});
        }
    } else if (key == QLatin1String(kPort)) {
        // Порты предлагаются для выбранного сейчас хоста; адрес можно ввести и именем, которое
        // нашёл обратный DNS.
        const QString chosen = settings.value(QLatin1String(kHost)).toString().trimmed();
        for (const NetworkScanner::Host &host : hosts) {
            if (chosen.compare(host.address.toString(), Qt::CaseInsensitive) != 0
                && (host.name.isEmpty() || chosen.compare(host.name, Qt::CaseInsensitive) != 0)) {
                continue;
            }
            for (int port : host.ports)
                options.append({QString::number(port), port});
        }
    }
    return options;
}

QString TcpTunnelPlugin::scanStatusText(NoteSeverity *severity) const
{
    *severity = NoteSeverity::Neutral;
    const int found = int(m_scanner.results().size());

    if (m_scanner.isRunning()) {
        return tr("Scanning %1: %2 of %3 addresses checked, found: %4")
            .arg(m_scanScope)
            .arg(m_scanner.hostsDone())
            .arg(m_scanner.targetCount())
            .arg(found);
    }
    if (m_scanScope.isEmpty())
        return {}; // поиск ещё ни разу не запускали: остаётся текст по умолчанию
    // Хосты, принимающие соединения на любой порт, в результат не входят; о них говорим отдельно:
    // иначе «ничего не найдено» рядом с хостом, который отвечает, выглядит ошибкой сканера.
    QString ignored;
    if (m_scanner.interceptedCount() > 0) {
        ignored = QLatin1Char(' ')
                  + tr("Ignored %1 address(es) that accept connections on every port: a router, VPN or "
                       "proxy answers for them.")
                        .arg(m_scanner.interceptedCount());
    }
    // «Ничего не найдено» — не сбой сканера, а предупреждение: так человек понимает, что искать надо
    // иначе (другая сеть, другой порт), а не что что-то сломалось.
    if (found == 0) {
        *severity = NoteSeverity::Warning;
        return tr("Nothing found in %1.").arg(m_scanScope) + ignored;
    }

    *severity = NoteSeverity::Ok;
    QString text = tr("Scan of %1 finished, found: %2").arg(m_scanScope).arg(found) + ignored;
    if (NetworkScanner::looksIntercepted(found, m_scanner.targetCount())) {
        *severity = NoteSeverity::Warning;
        text += QLatin1Char(' ')
                + tr("Almost every address answers: a router, VPN or proxy accepts connections "
                     "on behalf of the hosts, so this list is probably not real.");
    }
    return text;
}

void TcpTunnelPlugin::learnName(const QString &host, const QString &name)
{
    if (m_hostNames->remember(host, name)) {
        // Из очереди: сигнал уходит реестру, и пересчёт подписей не должен идти внутри liveOptions().
        QMetaObject::invokeMethod(&m_notifier, &TcpTunnelDeviceNotifier::devicesChanged, Qt::QueuedConnection);
    }
}

void TcpTunnelPlugin::learnScannedNames()
{
    for (const NetworkScanner::Host &host : m_scanner.results()) {
        if (!host.name.isEmpty())
            learnName(host.address.toString(), host.name);
    }
}

} // namespace spotty
