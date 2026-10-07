/**
 * \file SerialVirtualPort.cpp
 * \brief Реализация spotty::SerialVirtualPort.
 */
#include "SerialVirtualPort.h"

#include "UartProxyPorts.h"

#include <QSerialPort>

namespace spotty {

SerialVirtualPort::SerialVirtualPort(QString portName, QObject *parent)
    : VirtualPort(parent)
    , m_portName(std::move(portName))
{
}

SerialVirtualPort::~SerialVirtualPort()
{
    close();
}

bool SerialVirtualPort::open(const QVariantMap &settings, QString *error)
{
    m_port = new QSerialPort(m_portName, this);

    QString problem;
    if (!uartproxy::configureSerial(m_port, settings, &problem)
        || !m_port->open(QIODevice::ReadWrite)) {
        if (problem.isEmpty())
            problem = m_port->errorString();
        if (error)
            *error = tr("Virtual port %1: %2").arg(m_portName, problem);
        delete m_port;
        m_port = nullptr;
        return false;
    }

    connect(m_port, &QSerialPort::readyRead, this, [this] {
        const QByteArray chunk = m_port->readAll();
        if (!chunk.isEmpty())
            Q_EMIT dataRead(chunk);
    });
    connect(m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        // Остальное восстановимо или не про нас; пропавший порт — единственное, после чего
        // перехватывать дальше нечем.
        if (e == QSerialPort::ResourceError)
            Q_EMIT failed(tr("Virtual port %1 disappeared.").arg(m_portName));
    });
    return true;
}

void SerialVirtualPort::close()
{
    if (!m_port)
        return;
    m_port->disconnect(this);
    if (m_port->isOpen())
        m_port->close();
    delete m_port;
    m_port = nullptr;
}

void SerialVirtualPort::write(const QByteArray &data)
{
    if (m_port && m_port->isOpen())
        m_port->write(data);
}

} // namespace spotty
