/**
 * \file UartProxyChannel.cpp
 * \brief Реализация spotty::UartProxyChannel.
 */
#include "UartProxyChannel.h"

#include "SerialVirtualPort.h"
#include "UartProxyPorts.h"
#include "UartProxySettings.h"
#include "VirtualPort.h"

#include <QSerialPort>

#ifdef Q_OS_UNIX
#include "PtyPort.h"
#endif

#ifdef Q_OS_WIN
#include "Com0com.h"
#endif

namespace spotty {

using namespace uartproxy;

UartProxyChannel::UartProxyChannel(QObject *parent)
    : IInterfaceChannel(parent)
{
    // Порты создаются в open(): объект переедет в поток ввода-вывода, а всё, что
    // привязано к потоку, должно родиться уже там.
}

UartProxyChannel::~UartProxyChannel()
{
    teardown();
}

std::unique_ptr<VirtualPort> UartProxyChannel::createVirtualPort(const QVariantMap &settings,
                                                                 QString *error) const
{
    const QString existing = settings.value(QLatin1String(kVirtualPort)).toString().trimmed();

#ifdef Q_OS_WIN
    // На Windows в настройке — имя, которое открывает чужая программа, а Spotty нужен второй
    // конец пары. Пары может ещё не быть: тогда она создаётся здесь, в потоке ввода-вывода,
    // потому что установка устройств драйвером длится десятки секунд.
    if (existing.isEmpty()) {
        if (error)
            *error = tr("Choose the virtual port that the other program will open.");
        return nullptr;
    }
    int createdPair = -1;
    const QString hiddenEnd = ensureCom0comPair(existing, error, &createdPair);
    if (hiddenEnd.isEmpty())
        return nullptr;
    if (createdPair >= 0 && m_pairCreated)
        m_pairCreated(createdPair);
    return std::make_unique<SerialVirtualPort>(hiddenEnd, existing);
#else
    Q_UNUSED(error);

    // Готовый порт важнее созданного: его задали явно.
    if (!existing.isEmpty())
        return std::make_unique<SerialVirtualPort>(existing);

    return std::make_unique<PtyPort>(
        settings.value(QLatin1String(kLinkPath)).toString().trimmed());
#endif
}

bool UartProxyChannel::open(const QVariantMap &settings, QString *error)
{
    if (m_state == ChannelState::Open)
        return true;

    setState(ChannelState::Opening);

    const auto reject = [this, error](const QString &message) {
        teardown();
        if (error)
            *error = message;
        setState(ChannelState::Error, message);
        return false;
    };

    const QString realValue = settings.value(QLatin1String(kRealPort)).toString();
    if (realValue.isEmpty())
        return reject(tr("Choose the real port with the device."));

    m_real = new QSerialPort(resolvePortName(realValue), this);

    QString problem;
    if (!configureSerial(m_real, settings, &problem))
        return reject(problem);
    if (!m_real->open(QIODevice::ReadWrite))
        return reject(tr("%1: %2").arg(m_real->portName(), m_real->errorString()));

    // Как у обычного порта: линии выставляются сразу, их состояние в момент открытия
    // решает, перезагрузится ли плата.
    m_real->setDataTerminalReady(settings.value(QLatin1String(kDtrOnOpen)).toBool());
    m_real->setRequestToSend(settings.value(QLatin1String(kRtsOnOpen)).toBool());

    m_virtual = createVirtualPort(settings, &problem);
    if (!m_virtual)
        return reject(problem);
    if (!m_virtual->open(settings, &problem))
        return reject(problem);

    m_clock.start();

    connect(m_real, &QSerialPort::readyRead, this, &UartProxyChannel::onRealReadyRead);
    connect(m_real, &QSerialPort::errorOccurred, this, &UartProxyChannel::onRealError);
    connect(m_virtual.get(), &VirtualPort::dataRead, this, &UartProxyChannel::onVirtualData);
    connect(m_virtual.get(), &VirtualPort::failed, this, &UartProxyChannel::fail);
    connect(m_virtual.get(), &VirtualPort::overflowed, this, [this] {
        Q_EMIT errorOccurred(tr("The program on the virtual port is not reading: "
                                "its data is being dropped (capture is not affected)."));
    });

    m_openedReal = realValue;
    m_openedVirtual = m_virtual->description();

    setState(ChannelState::Open, tr("Virtual port: %1").arg(m_virtual->description()));
    return true;
}

void UartProxyChannel::teardown()
{
    if (m_virtual) {
        m_virtual->disconnect(this);
        m_virtual->close();
        m_virtual.release()->deleteLater();
    }
    if (m_real) {
        m_real->disconnect(this);
        if (m_real->isOpen())
            m_real->close();
        m_real->deleteLater();
        m_real = nullptr;
    }
}

void UartProxyChannel::close()
{
    // Хвост ответа устройства забирается до закрытия, как у обычного порта: потерять
    // последние байты, возможно, самого интересного обмена было бы обидно.
    if (m_real && m_real->isOpen())
        onRealReadyRead();

    teardown();
    setState(ChannelState::Closed);
}

qint64 UartProxyChannel::write(const QByteArray &data)
{
    if (!m_real || !m_real->isOpen())
        return -1;
    return m_real->write(data);
}

bool UartProxyChannel::applySettings(const QVariantMap &settings)
{
    if (!m_real || m_state != ChannelState::Open)
        return false;

    // Смена любого из портов — это уже другое соединение.
    if (settings.value(QLatin1String(kRealPort)).toString() != m_openedReal)
        return false;
    const QString virtualValue = settings.value(QLatin1String(kVirtualPort)).toString().trimmed();
    const QString linkValue = settings.value(QLatin1String(kLinkPath)).toString().trimmed();
    const QString desired = virtualValue.isEmpty() ? linkValue : virtualValue;
    if (!desired.isEmpty() && desired != m_openedVirtual)
        return false;

    QString problem;
    if (!configureSerial(m_real, settings, &problem)) {
        Q_EMIT errorOccurred(problem);
        return false;
    }
    return true;
}

void UartProxyChannel::onRealReadyRead()
{
    if (!m_real)
        return;

    const QByteArray chunk = m_real->readAll();
    if (chunk.isEmpty())
        return;

    // Отметка — по факту чтения, до любой другой работы: из неё считаются паузы и
    // порядок направлений.
    Q_EMIT dataReceived(chunk, m_clock.nsecsElapsed());

    if (m_virtual)
        m_virtual->write(chunk);
}

void UartProxyChannel::onVirtualData(const QByteArray &data)
{
    if (!m_real || !m_real->isOpen())
        return;

    Q_EMIT dataTransmitted(data, m_clock.nsecsElapsed());
    m_real->write(data);
}

void UartProxyChannel::onRealError()
{
    if (!m_real)
        return;

    const QSerialPort::SerialPortError error = m_real->error();
    if (error == QSerialPort::NoError)
        return;

    const QString message = m_real->errorString();
    m_real->clearError();

    switch (error) {
    case QSerialPort::ReadError:
        // Кадрирование, чётность, переполнение: восстановимо (подробности — в UartChannel).
        Q_EMIT errorOccurred(message);
        break;
    case QSerialPort::TimeoutError:
    case QSerialPort::NotOpenError:
    case QSerialPort::NoError:
        break;
    default:
        // Остальное — порта больше нет или писать некуда; перехватывать дальше нечем.
        fail(message);
        break;
    }
}

void UartProxyChannel::fail(const QString &message)
{
    Q_EMIT errorOccurred(message);
    teardown();
    setState(ChannelState::Error, message);
}

void UartProxyChannel::setState(ChannelState state, const QString &detail)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(m_state, detail);
}

} // namespace spotty
