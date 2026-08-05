/**
 * \file JlinkRttChannel.h
 * \brief Канал одного RTT-буфера через SEGGER J-Link.
 */
#pragma once

#include <spotty/api/IInterfaceChannel.h>

#include <QElapsedTimer>

class QTimer;

namespace spotty {

/**
 * \class JlinkRttChannel
 * \brief Один канал RTT (номер буфера) на одном зонде J-Link.
 *
 * \par Опрос вместо событий
 *
 * RTT — не событийный протокол: библиотека не умеет сообщить «данные пришли», только
 * ответить на вопрос «что накопилось сейчас». Поэтому вместо сигнала readyRead — таймер,
 * как в LoopbackChannel, только опрашивающий настоящее устройство, а не генератор.
 *
 * \par Асинхронный поиск управляющего блока
 *
 * Запуск RTT в библиотеке не означает, что она уже нашла в памяти таргета структуру
 * `SEGGER RTT` — поиск идёт в фоне. Пока он не завершился, отсутствие данных — это норма,
 * а не сбой. Если через несколько секунд блок так и не найден, канал сообщает об этом
 * errorOccurred(), не меняя состояния: возможно, RTT на таргете просто не инициализирован.
 */
class JlinkRttChannel : public IInterfaceChannel
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param serialNumber Серийный номер зонда.
     * \param channel Номер буфера RTT (0 — обычно «Terminal»).
     */
    JlinkRttChannel(quint32 serialNumber, int channel, QObject *parent = nullptr);
    ~JlinkRttChannel() override;

    /// \copydoc spotty::IInterfaceChannel::open
    bool open(const QVariantMap &settings, QString *error) override;

    /// \copydoc spotty::IInterfaceChannel::close
    void close() override;

    /// \copydoc spotty::IInterfaceChannel::write
    qint64 write(const QByteArray &data) override;

    /// \copydoc spotty::IInterfaceChannel::state
    ChannelState state() const override { return m_state; }

private:
    /// \brief Опросить буфер и выдать накопленное сигналом.
    void poll();

    /// \brief Сменить состояние и известить, если оно действительно изменилось.
    void setState(ChannelState state, const QString &detail = {});

    quint32 m_serialNumber;
    int m_channel;
    ChannelState m_state = ChannelState::Closed;

    QTimer *m_pollTimer = nullptr;
    QElapsedTimer m_clock; ///< Монотонные часы для меток времени принятых данных.

    bool m_controlBlockFound = false; ///< Автопоиск на стороне библиотеки уже нашёл блок.
    int m_waitTicks = 0;              ///< Сколько опросов подряд блок ещё не найден.
};

} // namespace spotty
