/**
 * \file JlinkRttChannel.cpp
 * \brief Реализация spotty::JlinkRttChannel.
 */
#include "JlinkRttChannel.h"

#include "JlinkArmLibrary.h"

#include <QTimer>

namespace spotty {

namespace {

// Ключи настроек; должны совпадать со схемой в JlinkRttPlugin::settingsSchema().
constexpr auto kTargetInterface = "targetInterface";
constexpr auto kSpeedKhz = "speedKhz";
constexpr auto kTargetDevice = "targetDevice";

constexpr int kPollIntervalMs = 20;   ///< Опрос RTT; отзывчиво, но не насилует зонд по USB.
constexpr int kReadChunkSize = 4096;  ///< Максимум байт за один опрос.

/// \brief Сколько опросов подряд ждать управляющий блок, прежде чем предупредить.
constexpr int kWarnAfterTicks = (3000 / kPollIntervalMs);

} // namespace

JlinkRttChannel::JlinkRttChannel(quint32 serialNumber, int channel, QObject *parent)
    : IInterfaceChannel(parent)
    , m_serialNumber(serialNumber)
    , m_channel(channel)
{
}

JlinkRttChannel::~JlinkRttChannel()
{
    close();
}

bool JlinkRttChannel::open(const QVariantMap &settings, QString *error)
{
    JlinkArmLibrary &lib = JlinkArmLibrary::instance();

    QString detail;
    if (!lib.openConnection(m_serialNumber, &detail)) {
        if (error) {
            *error = tr("Could not open J-Link S/N %1: %2")
                        .arg(m_serialNumber)
                        .arg(detail);
        }
        return false;
    }

    const QString interfaceType = settings.value(QLatin1String(kTargetInterface)).toString();
    const int speedKhz = settings.value(QLatin1String(kSpeedKhz)).toInt();
    lib.selectInterfaceAndSpeed(
        interfaceType == QLatin1String("jtag") ? JlinkArmLibrary::Jtag : JlinkArmLibrary::Swd,
        speedKhz);

    const QString targetDevice = settings.value(QLatin1String(kTargetDevice)).toString();
    if (!lib.connectTarget(targetDevice, &detail)) {
        lib.closeConnection();
        if (error)
            *error = tr("Could not connect to the target: %1").arg(detail);
        return false;
    }

    if (!lib.rttStart(&detail)) {
        lib.closeConnection();
        if (error)
            *error = tr("Could not start RTT: %1").arg(detail);
        return false;
    }

    m_clock.start();
    m_controlBlockFound = false;
    m_waitTicks = 0;

    // Таймер создаётся здесь, а не в конструкторе: к моменту open() канал уже перенесён в
    // поток ввода-вывода, и таймер должен принадлежать именно ему.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &JlinkRttChannel::poll);
    m_pollTimer->start();

    setState(ChannelState::Open, tr("RTT%1").arg(m_channel));
    return true;
}

void JlinkRttChannel::close()
{
    if (m_pollTimer) {
        m_pollTimer->stop();
        delete m_pollTimer;
        m_pollTimer = nullptr;
    }

    if (m_state == ChannelState::Open || m_state == ChannelState::Opening) {
        JlinkArmLibrary &lib = JlinkArmLibrary::instance();
        lib.rttStop();
        lib.closeConnection();
    }

    setState(ChannelState::Closed);
}

qint64 JlinkRttChannel::write(const QByteArray &data)
{
    if (m_state != ChannelState::Open)
        return -1;
    return JlinkArmLibrary::instance().rttWrite(m_channel, data);
}

void JlinkRttChannel::poll()
{
    bool running = false;
    const QByteArray data = JlinkArmLibrary::instance().rttRead(m_channel, kReadChunkSize,
                                                                 &running);
    if (!data.isEmpty())
        Q_EMIT dataReceived(data, m_clock.nsecsElapsed());

    if (running) {
        m_controlBlockFound = true;
        return;
    }

    if (m_controlBlockFound)
        return; // Был найден и не пропадёт снова — искать заново незачем.

    if (++m_waitTicks == kWarnAfterTicks) {
        Q_EMIT errorOccurred(tr("RTT control block not found on the target yet — check that "
                                "RTT is initialised in firmware and the target is running."));
    }
}

void JlinkRttChannel::setState(ChannelState state, const QString &detail)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(m_state, detail);
}

} // namespace spotty
