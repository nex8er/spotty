/**
 * \file NabChannel.h
 * \brief Канал одного bulk-интерфейса NAB.
 */
#pragma once

#include <spotty/api/IInterfaceChannel.h>

#include "LibusbKLibrary.h"

#include <QElapsedTimer>

class QTimer;

namespace spotty {

/**
 * \class NabChannel
 * \brief Отдельный терминальный канал одного USB-интерфейса NAB.
 *
 * libusbK не уведомляет Qt о готовности bulk endpoint-а. Канал коротко ожидает чтение в
 * собственном потоке ввода-вывода и запускает следующую попытку таймером; так UI не
 * блокируется, а close() никогда не ждёт незавершаемую операцию драйвера.
 */
class NabChannel : public IInterfaceChannel
{
    Q_OBJECT

public:
    NabChannel(QString identity, int interfaceNumber, QObject *parent = nullptr);
    ~NabChannel() override;

    /// \copydoc spotty::IInterfaceChannel::open
    bool open(const QVariantMap &settings, QString *error) override;

    /// \copydoc spotty::IInterfaceChannel::close
    void close() override;

    /// \copydoc spotty::IInterfaceChannel::write
    qint64 write(const QByteArray &data) override;

    /// \copydoc spotty::IInterfaceChannel::state
    ChannelState state() const override { return m_state; }

private:
    /// \brief Прочитать накопленные байты с bulk IN endpoint-а.
    void poll();

    /// \brief Освободить ручку libusbK, сохранив текущее состояние канала.
    void releaseHandle();

    /// \brief Перевести ошибку передачи в состояние канала.
    void handleTransferFailure(quint32 errorCode, bool reportError);

    /// \brief Сменить состояние и известить, если оно действительно изменилось.
    void setState(ChannelState state, const QString &detail = {});

    QString m_identity;
    int m_interfaceNumber = -1;
    void *m_handle = nullptr;
    LibusbKLibrary::PipeInfo m_input;
    LibusbKLibrary::PipeInfo m_output;
    ChannelState m_state = ChannelState::Closed;
    QTimer *m_pollTimer = nullptr;
    QElapsedTimer m_clock;
};

} // namespace spotty
