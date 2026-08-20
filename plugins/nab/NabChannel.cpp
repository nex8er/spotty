/**
 * \file NabChannel.cpp
 * \brief Реализация spotty::NabChannel.
 */
#include "NabChannel.h"

#include <QTimer>

namespace spotty {

namespace {

constexpr int kPollIntervalMs = 10;
constexpr int kReadTimeoutMs = 5;
constexpr int kWriteTimeoutMs = 500;
constexpr int kReadChunkSize = 4096;
constexpr quint32 kTimeoutError = 1460; // ERROR_TIMEOUT
constexpr quint32 kSemaphoreTimeoutError = 121; // ERROR_SEM_TIMEOUT
constexpr quint32 kWaitTimeoutError = 258; // WAIT_TIMEOUT

bool isTimeout(quint32 errorCode)
{
    return errorCode == kTimeoutError || errorCode == kSemaphoreTimeoutError
        || errorCode == kWaitTimeoutError;
}

} // namespace

NabChannel::NabChannel(QString identity, int interfaceNumber, QObject *parent)
    : IInterfaceChannel(parent)
    , m_identity(std::move(identity))
    , m_interfaceNumber(interfaceNumber)
{
}

NabChannel::~NabChannel()
{
    close();
}

bool NabChannel::open(const QVariantMap &settings, QString *error)
{
    Q_UNUSED(settings);

    if (m_state == ChannelState::Open)
        return true;

    setState(ChannelState::Opening);

    QString detail;
    if (!LibusbKLibrary::instance().open(m_identity, m_interfaceNumber, &m_handle, &m_input,
                                         &m_output, &detail)) {
        if (error)
            *error = tr("Could not open NAB USB interface %1: %2")
                         .arg(m_interfaceNumber)
                         .arg(detail);
        setState(ChannelState::Error, error ? *error : detail);
        return false;
    }

    // Синхронный ReadPipe без лимита может ждать бесконечно. Это недопустимо даже в
    // отдельном потоке: остановка сессии должна закрываться сразу, а не зависеть от того,
    // решит ли устройство когда-нибудь послать следующий пакет.
    if (!LibusbKLibrary::instance().setTransferTimeout(m_handle, m_input.address,
                                                       kReadTimeoutMs, &detail)
        || !LibusbKLibrary::instance().setTransferTimeout(m_handle, m_output.address,
                                                           kWriteTimeoutMs, &detail)) {
        releaseHandle();
        if (error)
            *error = tr("Could not configure NAB USB interface %1: %2")
                         .arg(m_interfaceNumber)
                         .arg(detail);
        setState(ChannelState::Error, error ? *error : detail);
        return false;
    }

    m_clock.start();
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &NabChannel::poll);
    m_pollTimer->start();

    setState(ChannelState::Open, tr("USB interface %1").arg(m_interfaceNumber));
    return true;
}

void NabChannel::close()
{
    if (m_pollTimer) {
        m_pollTimer->stop();
        delete m_pollTimer;
        m_pollTimer = nullptr;
    }
    releaseHandle();
    setState(ChannelState::Closed);
}

qint64 NabChannel::write(const QByteArray &data)
{
    if (!m_handle || m_state != ChannelState::Open)
        return -1;

    const LibusbKLibrary::TransferResult result =
        LibusbKLibrary::instance().write(m_handle, m_output.address, data);
    if (result.ok)
        return result.transferred;

    handleTransferFailure(result.errorCode, false);
    return -1;
}

void NabChannel::poll()
{
    if (!m_handle || m_state != ChannelState::Open)
        return;

    QByteArray data;
    const LibusbKLibrary::TransferResult result =
        LibusbKLibrary::instance().read(m_handle, m_input.address, kReadChunkSize, &data);
    if (result.ok) {
        if (!data.isEmpty())
            Q_EMIT dataReceived(data, m_clock.nsecsElapsed());
        return;
    }

    if (isTimeout(result.errorCode))
        return;
    handleTransferFailure(result.errorCode, true);
}

void NabChannel::releaseHandle()
{
    if (m_handle) {
        LibusbKLibrary::instance().close(m_handle);
        m_handle = nullptr;
    }
    m_input = {};
    m_output = {};
}

void NabChannel::handleTransferFailure(quint32 errorCode, bool reportError)
{
    const QString detail = LibusbKLibrary::errorText(errorCode);
    if (reportError)
        Q_EMIT errorOccurred(tr("NAB USB transfer failed: %1").arg(detail));

    if (m_pollTimer)
        m_pollTimer->stop();
    releaseHandle();

    if (LibusbKLibrary::isDisconnectError(errorCode))
        setState(ChannelState::Unavailable, detail);
    else
        setState(ChannelState::Error, detail);
}

void NabChannel::setState(ChannelState state, const QString &detail)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(m_state, detail);
}

} // namespace spotty
