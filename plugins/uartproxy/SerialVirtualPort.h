/**
 * \file SerialVirtualPort.h
 * \brief Виртуальный конец перехвата на готовом последовательном порте.
 */
#pragma once

#include "VirtualPort.h"

class QSerialPort;

namespace spotty {

/**
 * \class SerialVirtualPort
 * \brief Виртуальный конец — уже существующий порт, чаще всего половина пары com0com.
 *
 * Единственный вариант на Windows, где создать виртуальный порт без драйвера нельзя:
 * пара ставится отдельно, Spotty занимает один её конец, чужая программа — другой.
 * Годится и на POSIX, если пару создали заранее (`socat`).
 *
 * Скорость и формат кадра берутся те же, что у реального порта: у настоящей пары они не
 * имеют значения, а у связки с физическим переходником совпадение исключает сюрпризы.
 */
class SerialVirtualPort : public VirtualPort
{
    Q_OBJECT

public:
    explicit SerialVirtualPort(QString portName, QObject *parent = nullptr);
    ~SerialVirtualPort() override;

    bool open(const QVariantMap &settings, QString *error) override;
    void close() override;
    void write(const QByteArray &data) override;
    QString description() const override { return m_portName; }

private:
    QString m_portName;
    QSerialPort *m_port = nullptr;
};

} // namespace spotty
