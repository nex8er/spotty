/**
 * \file TcpTunnelChannel.h
 * \brief Канал к агенту spotty-uartd на удалённой машине.
 */
#pragma once

#include <spotty/api/IInterfaceChannel.h>

#include <QElapsedTimer>
#include <QString>

class QTcpSocket;
class QTimer;

namespace spotty {

/**
 * \class TcpTunnelChannel
 * \brief Raw TCP до агента, который отдаёт UART удалённой машины.
 *
 * \par Что передаётся
 *
 * Ровно то, что идёт через порт: никакой рамки, заголовка или управляющих команд. Поэтому
 * линий DTR/RTS и смены скорости у канала нет — они остаются на стороне агента, где
 * заданы при установке (agent/spotty-uartd.c).
 *
 * \par Обрыв соединения
 *
 * Плата перезагружается, агент перезапускают, пропадает Wi-Fi, а пользователь при этом
 * сидит за терминалом и не хочет открывать порт заново. Поэтому при включённом
 * autoReconnect канал, потеряв соединение, переходит в ChannelState::Unavailable и сам
 * пробует снова каждые kRetryMs. Состояние Open возвращается не сразу после установки
 * соединения, а когда оно продержалось kSettleMs: агент без UART принимает клиента и
 * тут же отключает, и без паузы индикатор мигал бы «открыт — недоступен» раз в
 * kRetryMs.
 */
class TcpTunnelChannel : public IInterfaceChannel
{
    Q_OBJECT

public:
    explicit TcpTunnelChannel(QObject *parent = nullptr);
    ~TcpTunnelChannel() override;

    /// \copydoc spotty::IInterfaceChannel::open
    bool open(const QVariantMap &settings, QString *error) override;

    /// \copydoc spotty::IInterfaceChannel::close
    void close() override;

    /// \copydoc spotty::IInterfaceChannel::write
    qint64 write(const QByteArray &data) override;

    /// \copydoc spotty::IInterfaceChannel::state
    ChannelState state() const override { return m_state; }

    /// \brief Пауза между попытками переподключения, мс.
    static constexpr int kRetryMs = 2000;

    /// \brief Сколько соединение должно продержаться, чтобы считаться открытым после обрыва, мс.
    static constexpr int kSettleMs = 400;

private:
    void onReadyRead();
    void onDisconnected();
    void onConnected();

    /// \brief Запустить очередную попытку соединения (не блокируя поток).
    void attemptReconnect();

    /// \brief Освободить сокет и таймеры. Состояние не трогает.
    void teardown();

    /// \brief Открытие не удалось: всё освободить, сообщить причину и перейти в Error. \return всегда `false`.
    bool failOpen(const QString &message, QString *error);

    /// \brief Сменить состояние и известить, если оно действительно изменилось.
    void setState(ChannelState state, const QString &detail = {});

    QTcpSocket *m_socket = nullptr;
    QTimer *m_retryTimer = nullptr;   ///< Пауза между попытками переподключения.
    QTimer *m_settleTimer = nullptr;  ///< Выдержка перед возвратом в Open после обрыва.
    QElapsedTimer m_clock;            ///< Монотонные часы для меток времени приёма.

    QString m_host;
    quint16 m_port = 0;
    bool m_autoReconnect = true;

    /// \brief Идёт переподключение после обрыва, а не первое открытие.
    bool m_reconnecting = false;

    ChannelState m_state = ChannelState::Closed;
};

} // namespace spotty
