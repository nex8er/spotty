/**
 * \file UartProxyPorts.cpp
 * \brief Реализация общих функций работы с портами.
 */
#include "UartProxyPorts.h"

#include "UartProxySettings.h"

#include <QCoreApplication>
#include <QSerialPort>

namespace spotty::uartproxy {

namespace {

constexpr auto kIdPrefix = "uartproxy:";

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

QString stablePortId(const QSerialPortInfo &info)
{
    if (info.hasVendorIdentifier() && info.hasProductIdentifier()) {
        const QString serial = info.serialNumber();
        return QStringLiteral("%1%2:%3:%4")
            .arg(QLatin1String(kIdPrefix))
            .arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0'))
            .arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))
            // Два одинаковых переходника без серийного номера различаются хотя бы именем.
            .arg(serial.isEmpty() ? info.portName() : serial);
    }
    return QLatin1String(kIdPrefix) + info.portName();
}

QString resolvePortName(const QString &value)
{
    // Не пытаться разбирать идентификатор: он служит только ключом сравнения, а
    // единственное, что нужно из него получить, — имя нынешнего узла.
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        if (stablePortId(info) == value)
            return info.systemLocation();
    }
    return value;
}

QList<SettingsOption> portOptions(bool byName)
{
    QList<SettingsOption> options;
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    options.reserve(ports.size());

    for (const QSerialPortInfo &info : ports) {
        QString label = info.portName();
        if (!info.description().isEmpty())
            label += QStringLiteral(" — ") + info.description();
        options.append({label, byName ? info.portName() : stablePortId(info)});
    }
    return options;
}

bool configureSerial(QSerialPort *port, const QVariantMap &settings, QString *error)
{
    const int baudRate = settings.value(QLatin1String(kBaudRate)).toInt();
    if (baudRate <= 0) {
        if (error)
            *error = QCoreApplication::translate("spotty::UartProxyChannel", "Invalid baud rate.");
        return false;
    }

    if (!port->setBaudRate(baudRate)) {
        if (error) {
            *error = QCoreApplication::translate("spotty::UartProxyChannel",
                                                 "The port does not support %1 baud.")
                         .arg(baudRate);
        }
        return false;
    }

    port->setDataBits(QSerialPort::DataBits(settings.value(QLatin1String(kDataBits), 8).toInt()));
    port->setParity(parityFromString(settings.value(QLatin1String(kParity)).toString()));
    port->setStopBits(stopBitsFromString(settings.value(QLatin1String(kStopBits)).toString()));
    port->setFlowControl(
        flowControlFromString(settings.value(QLatin1String(kFlowControl)).toString()));
    return true;
}

} // namespace spotty::uartproxy
