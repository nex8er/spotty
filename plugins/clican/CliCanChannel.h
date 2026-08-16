/**
 * \file CliCanChannel.h
 * \brief Канал командной строки платы, туннелированной через CAN.
 */
#pragma once

#include "CanBus.h"

#include <spotty/api/IInterfaceChannel.h>

#include <QByteArray>
#include <QElapsedTimer>

#include <memory>

class QTimer;

namespace spotty {

/**
 * \class CliCanChannel
 * \brief Туннель UART-CLI одной платы поверх шины CAN.
 *
 * \par Что делает транспорт
 *
 * Отправленное пользователем режется на куски по 8 байт и уходит кадрами на приёмный
 * идентификатор узла; пришедшее с идентификатора ответа отдаётся ядру как есть. Границы
 * кадров смысла не несут — на той стороне байты складываются в приёмный буфер UART, и
 * строки из них собирает пакетизатор ядра, ровно как для настоящего порта.
 *
 * \par Почему нужны пустые пакеты
 *
 * Плата остаётся в режиме туннелирования 5 секунд после последнего пакета на свой адрес, а
 * потом возвращается к обычному UART и перестаёт слать вывод в CAN. Пока канал открыт, а
 * пользователь ничего не набирает, режим держится пустыми пакетами
 * (clican::kTunnelHoldMs). Отсюда же берётся смысл настройки «Keep-alive»: это не запас
 * прочности, а обязательство перед платой.
 *
 * \par Управление потоком
 *
 * Шлюз, у которого переполняется очередь, шлёт всем пакет 0x400 длиной 1 байт с числом
 * миллисекунд паузы. Канал в это время копит отправляемое в очереди, а не теряет его:
 * пользователь набрал команду, и молча выбросить её нельзя.
 */
class CliCanChannel : public IInterfaceChannel
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param handle Ручка канала PCAN, к которому подключён адаптер.
     * \param pool Пул шин плагина, создавшего канал; канал не переживает свой плагин, и
     *             владение пулом остаётся у него (см. \par «Почему не синглтон» в
     *             CanBus.h) — указатель, а не ссылка, потому что тип SDK
     *             (spotty::IInterfacePlugin::createChannel()) его не предусматривает.
     */
    explicit CliCanChannel(PcanLibrary::Handle handle, CanBusPool *pool,
                           QObject *parent = nullptr);
    ~CliCanChannel() override;

    /// \copydoc spotty::IInterfaceChannel::open
    bool open(const QVariantMap &settings, QString *error) override;

    /// \copydoc spotty::IInterfaceChannel::close
    void close() override;

    /// \copydoc spotty::IInterfaceChannel::write
    qint64 write(const QByteArray &data) override;

    /// \copydoc spotty::IInterfaceChannel::state
    ChannelState state() const override { return m_state; }

private:
    /// \brief Кадр с шины — уже в потоке ввода-вывода канала.
    void handleFrame(quint32 id, const QByteArray &data, qint64 monotonicNs);

    /// \brief Тик обслуживания: пустой пакет для удержания туннеля и проверка молчания узла.
    void tick();

    /// \brief Отправить накопленное, сколько позволяет пауза управления потоком.
    void drainQueue();

    /// \brief Сменить состояние и известить, если оно действительно изменилось.
    void setState(ChannelState state, const QString &detail = {});

    PcanLibrary::Handle m_handle;
    CanBusPool *m_pool = nullptr;
    int m_node = 0;
    int m_keepAliveMs = 1000;
    int m_responseTimeoutMs = 3000;

    ChannelState m_state = ChannelState::Closed;

    std::shared_ptr<CanBus> m_bus;
    int m_handlerToken = 0;

    QTimer *m_timer = nullptr;  ///< Обслуживание: удержание туннеля и надзор за молчанием.
    QElapsedTimer m_clock;      ///< Монотонные часы для пауз и таймаутов.

    QByteArray m_pending;       ///< Ещё не отправленное: пауза шлюза или занятая очередь.
    qint64 m_pausedUntilMs = 0; ///< До какого момента шлюз просил не отправлять.
    qint64 m_lastFrameSentMs = 0; ///< Когда узлу уходил последний пакет — любой.
    qint64 m_lastResponseMs = 0;  ///< Когда узел отвечал в последний раз.

    /// \brief О молчании узла уже сказано; повторять при каждом тике не нужно.
    bool m_silenceReported = false;
};

} // namespace spotty
