/**
 * \file UartChannel.h
 * \brief Канал последовательного порта.
 */
#pragma once

#include <spotty/api/IInterfaceChannel.h>

#include <QElapsedTimer>

class QSerialPort;
class QTimer;

namespace spotty {

/**
 * \class UartChannel
 * \brief Соединение с последовательным портом поверх QSerialPort.
 *
 * \par Линии управления
 *
 * QSerialPort не сообщает об изменении входных линий сигналом, поэтому состояние
 * CTS, DSR, DCD и RI опрашивается таймером. Опрос идёт только при открытом порте и с
 * шагом, который человек воспринимает как мгновенный, но который не заметен в нагрузке.
 *
 * \par DTR и RTS при открытии
 *
 * Значения этих линий выставляются сразу после открытия, потому что для многих плат они
 * заведены на сброс и загрузчик. Плата с ESP32 или Arduino перезагружается от перепада
 * DTR — иногда это именно то, что нужно, иногда категорически нет, поэтому решение
 * оставлено пользователю в настройках.
 */
class UartChannel : public IInterfaceChannel
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param portName Системное имя порта: `"COM5"`, `"/dev/ttyUSB0"`.
     */
    explicit UartChannel(QString portName, QObject *parent = nullptr);
    ~UartChannel() override;

    /// \copydoc spotty::IInterfaceChannel::open
    bool open(const QVariantMap &settings, QString *error) override;

    /// \copydoc spotty::IInterfaceChannel::close
    void close() override;

    /// \copydoc spotty::IInterfaceChannel::write
    qint64 write(const QByteArray &data) override;

    /// \copydoc spotty::IInterfaceChannel::state
    ChannelState state() const override { return m_state; }

    /**
     * \brief Перенастроить открытый порт без разрыва соединения.
     *
     * QSerialPort позволяет менять скорость и режим на лету, поэтому метод переопределён:
     * перезакрытие дёрнуло бы DTR и перезагрузило плату.
     */
    bool applySettings(const QVariantMap &settings) override;

    /// \brief Состояние входных линий: CTS, DSR, DCD, RI.
    QVariantMap controlLines() const override;

    /// \brief Управление DTR и RTS.
    bool setControlLine(const QString &name, bool asserted) override;

    /// \brief Удерживать состояние BREAK указанное время.
    bool sendBreak(int milliseconds) override;

private:
    /// \brief Применить параметры к объекту порта.
    bool configure(const QVariantMap &settings, QString *error);

    /// \brief Прочитать накопленное и выдать сигналом.
    void readIncoming();

    /// \brief Обработать ошибку порта, отличив восстановимую от фатальной.
    void handleError();

    /// \brief Опросить входные линии и сообщить об изменении.
    void pollPinout();

    /// \brief Сменить состояние и известить, если оно действительно изменилось.
    void setState(ChannelState state, const QString &detail = {});

    QString m_portName;
    QSerialPort *m_port = nullptr;
    QTimer *m_pinoutTimer = nullptr;
    ChannelState m_state = ChannelState::Closed;

    /// \brief Монотонные часы для меток времени принятых данных.
    QElapsedTimer m_clock;

    /// \brief Предыдущее состояние линий — чтобы не слать сигнал на каждый опрос.
    QVariantMap m_lastPinout;
};

} // namespace spotty
