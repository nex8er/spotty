/**
 * \file RemoteHost.cpp
 * \brief Реализация функций из RemoteHost.h.
 */
#include "RemoteHost.h"

#include <QCoreApplication>
#include <QtEndian>
#include <QHash>
#include <QHostAddress>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace spotty::tcptunnel {

namespace {

/**
 * \brief Носитель tr() для свободных функций этого файла.
 *
 * Имя структуры совпадает с контекстом из макроса: lupdate берёт контекст из имени класса, у
 * которого вызван tr(), а во время работы действует контекст из Q_DECLARE_TR_FUNCTIONS. Разные
 * имена дали бы строки, которые собираются, но не переводятся.
 */
struct RemoteHost
{
    Q_DECLARE_TR_FUNCTIONS(RemoteHost)
};

/// \brief Что про ARM-окружение говорит заголовок ELF у /bin/sh.
struct ElfFacts
{
    bool valid = false;
    int bits = 0;
    bool hardFloat = false;
};

ElfFacts readElf(const QByteArray &elf)
{
    ElfFacts facts;
    // Нужны 40 байт: e_flags лежит по смещению 36. Порядок байт — только little-endian: все
    // поддерживаемые платформы такие.
    if (elf.size() < 40 || !elf.startsWith(QByteArray("\x7f" "ELF", 4)) || elf.at(5) != 1)
        return facts;
    const auto *bytes = reinterpret_cast<const uchar *>(elf.constData());
    constexpr quint32 kArmFloatHard = 0x400; // EF_ARM_ABI_FLOAT_HARD
    constexpr quint16 kMachineArm = 0x28;
    facts.valid = true;
    facts.bits = elf.at(4) == 2 ? 64 : 32;
    facts.hardFloat = qFromLittleEndian<quint16>(bytes + 18) == kMachineArm
                      && (qFromLittleEndian<quint32>(bytes + 36) & kArmFloatHard) != 0;
    return facts;
}

} // namespace

const HostInfo::Instance *HostInfo::instance(int port) const
{
    for (const Instance &candidate : instances) {
        if (candidate.port == port)
            return &candidate;
    }
    return nullptr;
}

QString HostInfo::summary() const
{
    QStringList parts;
    parts << (osName.isEmpty() ? system : osName);
    parts << (dpkgArch.isEmpty() ? machine : QStringLiteral("%1/%2").arg(machine, dpkgArch));
    parts << (systemd ? QStringLiteral("systemd") : RemoteHost::tr("no systemd"));
    return parts.join(QStringLiteral(", "));
}

HostInfo parseHostInfo(const QString &detectOutput)
{
    HostInfo info;
    const QStringList lines = detectOutput.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = line.left(equals);
        const QString value = line.mid(equals + 1).trimmed();

        // Простые строки разбираются таблицей: ключ скрипта → поле.
        static const QHash<QString, QString HostInfo::*> strings = {
            {QStringLiteral("uname_s"), &HostInfo::system},     {QStringLiteral("uname_m"), &HostInfo::machine},
            {QStringLiteral("dpkg_arch"), &HostInfo::dpkgArch}, {QStringLiteral("hostname"), &HostInfo::hostname},
            {QStringLiteral("os_name"), &HostInfo::osName},
        };
        const auto field = strings.constFind(key);
        if (field != strings.constEnd()) {
            info.*field.value() = value;
        } else if (key == QLatin1String("long_bit")) {
            info.longBit = value.toInt();
        } else if (key == QLatin1String("uid")) {
            bool ok = false;
            const int uid = value.toInt(&ok);
            info.uid = ok ? uid : -1;
        } else if (key == QLatin1String("elf")) {
            info.elf = QByteArray::fromHex(value.toLatin1());
        } else if (key == QLatin1String("systemd")) {
            info.systemd = value == QLatin1String("yes");
        } else if (key == QLatin1String("sudo")) {
            info.hasSudo = value == QLatin1String("yes");
        } else if (key == QLatin1String("installed")) {
            // «spotty-uartd 1.0.0» → «1.0.0»
            info.installedVersion = value.section(QLatin1Char(' '), -1);
        } else if (key == QLatin1String("instance")) {
            bool ok = false;
            const int port = value.section(QLatin1Char('|'), 0, 0).toInt(&ok);
            if (ok)
                info.instances.append({port, value.section(QLatin1Char('|'), 1).trimmed()});
        } else if (key == QLatin1String("listen_hex")) {
            bool ok = false;
            const int port = value.toInt(&ok, 16);
            if (ok && port > 0)
                info.listening.insert(port);
        }
    }

    chooseAgentArch(&info);
    return info;
}

void chooseAgentArch(HostInfo *info)
{
    info->agentArch.clear();
    info->archProblem.clear();

    if (info->system != QLatin1String("Linux")) {
        info->archProblem = info->system.isEmpty()
                                ? RemoteHost::tr("The machine did not report what system it runs.")
                                : RemoteHost::tr("The agent runs on Linux only, and this machine runs %1.").arg(info->system);
        return;
    }

    const QString &machine = info->machine;

    // Debian и родня говорят архитектуру пакетов сами, и это точнее uname: 32-битный
    // userland на 64-битном ядре (Raspberry Pi OS 32-bit на Pi 4) показывает uname = aarch64,
    // а исполняться должен armhf-бинарник.
    if (!info->dpkgArch.isEmpty()) {
        static const QHash<QString, QString> packages = {
            {QStringLiteral("amd64"), QStringLiteral("x86_64")},
            {QStringLiteral("i386"), QStringLiteral("i386")},
            {QStringLiteral("arm64"), QStringLiteral("aarch64")},
            {QStringLiteral("armel"), QStringLiteral("armel")},
            {QStringLiteral("riscv64"), QStringLiteral("riscv64")},
        };
        if (info->dpkgArch == QLatin1String("armhf")) {
            // armv6l — Raspberry Pi Zero и 1: armv7-бинарник на них не стартует.
            info->agentArch = machine.startsWith(QLatin1String("armv6")) ? QStringLiteral("armv6")
                                                                         : QStringLiteral("armv7");
        } else {
            info->agentArch = packages.value(info->dpkgArch);
        }
        if (info->agentArch.isEmpty())
            info->archProblem = RemoteHost::tr("No agent is built for the package architecture \"%1\".").arg(info->dpkgArch);
        return;
    }

    // Без dpkg — по uname, а у 32-битного ARM ещё и по заголовку ELF: «жёсткий» float или нет
    // из uname не узнать.
    const ElfFacts elf = readElf(info->elf);
    const auto arm32 = [&elf](const QString &hardFloatArch) {
        return elf.valid && !elf.hardFloat ? QStringLiteral("armel") : hardFloatArch;
    };
    static const QRegularExpression x86_32(QStringLiteral("^i[3-6]86$"));
    static const QRegularExpression arm64(QStringLiteral("^(aarch64|arm64|armv8[lb])$"));
    if (machine == QLatin1String("x86_64")) {
        info->agentArch = (info->longBit == 32) ? QStringLiteral("i386") : QStringLiteral("x86_64");
    } else if (x86_32.match(machine).hasMatch()) {
        info->agentArch = QStringLiteral("i386");
    } else if (arm64.match(machine).hasMatch()) {
        const bool userland32 = info->longBit == 32 || (elf.valid && elf.bits == 32);
        info->agentArch = userland32 ? arm32(QStringLiteral("armv7")) : QStringLiteral("aarch64");
    } else if (machine.startsWith(QLatin1String("armv7"))) {
        info->agentArch = arm32(QStringLiteral("armv7"));
    } else if (machine.startsWith(QLatin1String("armv6"))) {
        info->agentArch = arm32(QStringLiteral("armv6"));
    } else if (machine.startsWith(QLatin1String("armv5")) || machine.startsWith(QLatin1String("armv4"))) {
        info->archProblem = RemoteHost::tr("ARMv5 and older processors are not supported by the agent "
                               "(\"%1\").").arg(machine);
    } else if (machine == QLatin1String("riscv64")) {
        info->agentArch = QStringLiteral("riscv64");
    } else {
        info->archProblem = RemoteHost::tr("No agent is built for the architecture \"%1\".").arg(machine);
    }
}

int nextFreePort(const HostInfo &info, int first)
{
    int port = std::max(first, 1);
    auto taken = [&info](int candidate) {
        return info.listening.contains(candidate) || info.instance(candidate) != nullptr;
    };
    while (port <= 65535 && taken(port))
        ++port;
    return port;
}

QList<UartPort> parseUartScan(const QString &output)
{
    QList<UartPort> result;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QStringList columns = line.split(QLatin1Char('\t'), Qt::KeepEmptyParts);
        if (columns.size() < 3 || !columns.first().startsWith(QLatin1Char('/')))
            continue;
        result.append({columns.at(0).trimmed(), columns.at(1).trimmed(), columns.at(2).trimmed()});
    }
    return result;
}

const QList<int> &supportedBauds()
{
    static const QList<int> bauds = {1200,    2400,    4800,    9600,    19200,   38400,
                                     57600,   115200,  230400,  460800,  500000,  576000,
                                     921600,  1000000, 1152000, 1500000, 2000000, 2500000,
                                     3000000, 3500000, 4000000};
    return bauds;
}

QString validateInstallParams(const InstallParams &params)
{
    if (params.device.isEmpty())
        return RemoteHost::tr("Choose the serial port of the remote machine.");
    // Путь идёт в конфиг одной строкой: перевод строки или пробел по краям дописали бы туда
    // лишнюю настройку или изменили бы путь.
    static const QRegularExpression badPath(QStringLiteral("[\\x00-\\x1f\\x7f]|^\\s|\\s$"));
    if (!params.device.startsWith(QLatin1Char('/')) || params.device.size() > 250
        || params.device.contains(badPath)) {
        return RemoteHost::tr("The serial port must be an absolute path such as /dev/ttyUSB0.");
    }

    if (!supportedBauds().contains(params.baud))
        return RemoteHost::tr("Unsupported baud rate %1.").arg(params.baud);

    static const QRegularExpression format(QStringLiteral("^[5-8][NEOneo][12]$"));
    if (!format.match(params.format).hasMatch())
        return RemoteHost::tr("The frame format must look like 8N1.");

    if (params.flow != QLatin1String("none") && params.flow != QLatin1String("rtscts")
        && params.flow != QLatin1String("xonxoff")) {
        return RemoteHost::tr("Unknown flow control \"%1\".").arg(params.flow);
    }

    if (params.port < 1 || params.port > 65535)
        return RemoteHost::tr("The port must be between 1 and 65535.");

    static const QRegularExpression bind(QStringLiteral("^[0-9A-Za-z:.\\-]+$"));
    if (!bind.match(params.bind).hasMatch())
        return RemoteHost::tr("The listen address \"%1\" is not valid.").arg(params.bind);

    const QStringList tokens = params.allow.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                  Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(token);
        if (subnet.first.isNull())
            return RemoteHost::tr("\"%1\" in the list of allowed clients is not an address or network.").arg(token);
    }
    return {};
}

QString renderConfig(const QString &templateText, const InstallParams &params)
{
    // Список клиентов приводится к одному виду: что бы ни ввёл пользователь, в файле он
    // записан через запятую с пробелом.
    const QStringList tokens = params.allow.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                  Qt::SkipEmptyParts);

    QString text = templateText;
    text.replace(QLatin1String("@DEVICE@"), params.device);
    text.replace(QLatin1String("@BAUD@"), QString::number(params.baud));
    text.replace(QLatin1String("@FORMAT@"), params.format.toUpper());
    text.replace(QLatin1String("@FLOW@"), params.flow);
    text.replace(QLatin1String("@PORT@"), QString::number(params.port));
    text.replace(QLatin1String("@BIND@"), params.bind);
    text.replace(QLatin1String("@ALLOW@"), tokens.join(QStringLiteral(", ")));
    return text;
}

QString shellQuote(const QString &text)
{
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

} // namespace spotty::tcptunnel
