/**
 * \file UartChannel.cpp
 * \brief Реализация spotty::UartChannel.
 */
#include "UartChannel.h"

#include <QSerialPort>
#include <QTimer>

namespace spotty {

namespace {

// Ключи настроек; должны совпадать со схемой в UartPlugin::settingsSchema().
constexpr auto kBaudRate = "baudRate";
constexpr auto kDataBits = "dataBits";
constexpr auto kParity = "parity";
constexpr auto kStopBits = "stopBits";
constexpr auto kFlowControl = "flowControl";
constexpr auto kDtrOnOpen = "dtrOnOpen";
constexpr auto kRtsOnOpen = "rtsOnOpen";

/**
 * \brief Период опроса входных линий, мс.
 *
 * QSerialPort не извещает об их изменении, поэтому приходится спрашивать. Двести
 * миллисекунд человек воспринимает как мгновенную реакцию, а нагрузки не создают.
 */
constexpr int kPinoutPollMs = 200;

QSerialPort::Parity parityFromString(const QString &value)
{
    if (value == QLatin1String("E"))
        return QSerialPort::EvenParity;
    if (value == QLatin1String("O"))
        return QSerialPort::OddParity;
    if (value == QLatin1String("M"))
        return QSerialPort::MarkParity;
    if (value == QLatin1String("S"))
        return QSerialPort::SpaceParity;
    return QSerialPort::NoParity;
}

QSerialPort::StopBits stopBitsFromString(const QString &value)
{
    if (value == QLatin1String("1.5"))
        return QSerialPort::OneAndHalfStop;
    if (value == QLatin1String("2"))
        return QSerialPort::TwoStop;
    return QSerialPort::OneStop;
}

QSerialPort::FlowControl flowControlFromString(const QString &value)
{
    if (value == QLatin1String("hardware"))
        return QSerialPort::HardwareControl;
    if (value == QLatin1String("software"))
        return QSerialPort::SoftwareControl;
    return QSerialPort::NoFlowControl;
}

} // namespace

UartChannel::UartChannel(QString portName, QObject *parent)
    : IInterfaceChannel(parent)
    , m_portName(std::move(portName))
{
    // Ни QSerialPort, ни таймер здесь не создаются: объект ещё в потоке создателя, а
    // работать будет в потоке ввода-вывода. Всё, что привязано к потоку, — в open().
}

UartChannel::~UartChannel() = default;

bool UartChannel::open(const QVariantMap &settings, QString *error)
{
    if (m_state == ChannelState::Open)
        return true;

    setState(ChannelState::Opening);

    m_port = new QSerialPort(m_portName, this);

    if (!configure(settings, error)) {
        delete m_port;
        m_port = nullptr;
        setState(ChannelState::Error, error ? *error : QString());
        return false;
    }

    if (!m_port->open(QIODevice::ReadWrite)) {
        // Сообщение QSerialPort объясняет причину человеческим языком («Permission
        // denied», «Device or resource busy»), поэтому передаём его как есть.
        const QString message = m_port->errorString();
        if (error)
            *error = message;
        delete m_port;
        m_port = nullptr;
        setState(ChannelState::Error, message);
        return false;
    }

    // Линии выставляются сразу: у многих плат DTR и RTS заведены на сброс и загрузчик,
    // и их состояние в момент открытия определяет, перезагрузится плата или нет.
    m_port->setDataTerminalReady(settings.value(QLatin1String(kDtrOnOpen)).toBool());
    m_port->setRequestToSend(settings.value(QLatin1String(kRtsOnOpen)).toBool());

    m_clock.start();

    connect(m_port, &QSerialPort::readyRead, this, &UartChannel::readIncoming);
    connect(m_port, &QSerialPort::errorOccurred, this, &UartChannel::handleError);

    m_pinoutTimer = new QTimer(this);
    m_pinoutTimer->setInterval(kPinoutPollMs);
    connect(m_pinoutTimer, &QTimer::timeout, this, &UartChannel::pollPinout);
    m_pinoutTimer->start();

    m_lastPinout.clear();
    pollPinout();

    setState(ChannelState::Open);
    return true;
}

bool UartChannel::configure(const QVariantMap &settings, QString *error)
{
    const int baudRate = settings.value(QLatin1String(kBaudRate)).toInt();
    if (baudRate <= 0) {
        if (error)
            *error = tr("Invalid baud rate.");
        return false;
    }

    if (!m_port->setBaudRate(baudRate)) {
        if (error)
            *error = tr("The port does not support %1 baud.").arg(baudRate);
        return false;
    }

    m_port->setDataBits(QSerialPort::DataBits(
        settings.value(QLatin1String(kDataBits), 8).toInt()));
    m_port->setParity(parityFromString(settings.value(QLatin1String(kParity)).toString()));
    m_port->setStopBits(stopBitsFromString(settings.value(QLatin1String(kStopBits)).toString()));
    m_port->setFlowControl(
        flowControlFromString(settings.value(QLatin1String(kFlowControl)).toString()));

    return true;
}

bool UartChannel::applySettings(const QVariantMap &settings)
{
    if (!m_port || m_state != ChannelState::Open)
        return false;

    // Перенастройка на лету, а не перезакрытие: снятие DTR при закрытии перезагрузило бы
    // плату, и смена скорости обошлась бы потерей состояния устройства.
    QString error;
    if (!configure(settings, &error)) {
        Q_EMIT errorOccurred(error);
        return false;
    }
    return true;
}

void UartChannel::close()
{
    if (m_pinoutTimer) {
        m_pinoutTimer->stop();
        delete m_pinoutTimer;
        m_pinoutTimer = nullptr;
    }

    if (m_port) {
        // Хвост входного буфера забираем до закрытия: устройство могло ответить в самый
        // последний момент, и терять этот ответ нельзя.
        if (m_port->isOpen()) {
            readIncoming();
            m_port->close();
        }
        delete m_port;
        m_port = nullptr;
    }

    m_lastPinout.clear();
    setState(ChannelState::Closed);
}

qint64 UartChannel::write(const QByteArray &data)
{
    if (!m_port || !m_port->isOpen())
        return -1;
    return m_port->write(data);
}

void UartChannel::readIncoming()
{
    if (!m_port)
        return;

    const QByteArray chunk = m_port->readAll();
    if (chunk.isEmpty())
        return;

    // Отметка ставится здесь, сразу по факту чтения: из неё считаются межбайтовые паузы
    // для пакетизации, и задержка на обработку исказила бы разбиение потока на кадры.
    Q_EMIT dataReceived(chunk, m_clock.nsecsElapsed());
}

void UartChannel::handleError()
{
    if (!m_port)
        return;

    const QSerialPort::SerialPortError error = m_port->error();
    if (error == QSerialPort::NoError)
        return;

    const QString message = m_port->errorString();

    switch (error) {
    case QSerialPort::ReadError:
        // Восстановимая. В Qt 6 отдельных значений FramingError, ParityError и
        // BreakConditionError больше нет — они были объявлены устаревшими в Qt 5 и
        // удалены, а ошибки кадрирования, чётности и переполнения теперь приходят сюда,
        // под общим ReadError. Отличить их средствами QSerialPort нельзя; сообщение
        // драйвера в errorString() обычно называет причину, поэтому передаём его как есть.
        Q_EMIT errorOccurred(message);
        break;

    case QSerialPort::ResourceError:
        // Устройство исчезло из системы: выдернули кабель, отвалился переходник. Именно
        // Unavailable, а не Error — по нему ядро откроет порт заново, когда устройство
        // вернётся.
        Q_EMIT errorOccurred(message);
        if (m_pinoutTimer)
            m_pinoutTimer->stop();
        if (m_port->isOpen())
            m_port->close();
        setState(ChannelState::Unavailable, message);
        break;

    case QSerialPort::PermissionError:
    case QSerialPort::DeviceNotFoundError:
    case QSerialPort::OpenError:
    case QSerialPort::WriteError:
    case QSerialPort::UnsupportedOperationError:
    case QSerialPort::UnknownError:
        Q_EMIT errorOccurred(message);
        setState(ChannelState::Error, message);
        break;

    case QSerialPort::TimeoutError:
    case QSerialPort::NotOpenError:
    case QSerialPort::NoError:
        break;
    }

    m_port->clearError();
}

void UartChannel::pollPinout()
{
    const QVariantMap lines = controlLines();
    if (lines == m_lastPinout)
        return;

    m_lastPinout = lines;
    Q_EMIT controlLinesChanged();
}

QVariantMap UartChannel::controlLines() const
{
    if (!m_port || !m_port->isOpen())
        return {};

    const QSerialPort::PinoutSignals signals_ = m_port->pinoutSignals();
    return {
        {QStringLiteral("CTS"), signals_.testFlag(QSerialPort::ClearToSendSignal)},
        {QStringLiteral("DSR"), signals_.testFlag(QSerialPort::DataSetReadySignal)},
        {QStringLiteral("DCD"), signals_.testFlag(QSerialPort::DataCarrierDetectSignal)},
        {QStringLiteral("RI"), signals_.testFlag(QSerialPort::RingIndicatorSignal)},
        {QStringLiteral("DTR"), signals_.testFlag(QSerialPort::DataTerminalReadySignal)},
        {QStringLiteral("RTS"), signals_.testFlag(QSerialPort::RequestToSendSignal)},
    };
}

bool UartChannel::setControlLine(const QString &name, bool asserted)
{
    if (!m_port || !m_port->isOpen())
        return false;

    if (name == QLatin1String("DTR"))
        return m_port->setDataTerminalReady(asserted);
    if (name == QLatin1String("RTS"))
        return m_port->setRequestToSend(asserted);
    return false;
}

bool UartChannel::sendBreak(int milliseconds)
{
    if (!m_port || !m_port->isOpen())
        return false;

    if (!m_port->setBreakEnabled(true))
        return false;

    // Снятие через таймер, а не через ожидание: блокировать поток ввода-вывода на время
    // удержания BREAK значило бы задержать приём данных с других каналов.
    QTimer::singleShot(qMax(1, milliseconds), this, [this] {
        if (m_port && m_port->isOpen())
            m_port->setBreakEnabled(false);
    });
    return true;
}

void UartChannel::setState(ChannelState state, const QString &detail)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(m_state, detail);
}

} // namespace spotty
