/**
 * \file Packetizer.cpp
 * \brief Реализация spotty::Packetizer.
 */
#include "Packetizer.h"

namespace spotty {

void Packetizer::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    // Остаток принадлежал прежнему правилу разбиения и по новому смысла не имеет.
    m_pending.clear();
    m_mode = mode;
}

void Packetizer::setTimeoutMs(int timeoutMs)
{
    m_timeoutMs = qMax(1, timeoutMs);
}

void Packetizer::setDelimiter(const QByteArray &delimiter)
{
    m_delimiter = delimiter;
}

void Packetizer::setFixedLength(int length)
{
    m_fixedLength = qMax(1, length);
}

void Packetizer::reset()
{
    m_pending.clear();
    m_lastByteNs = 0;
    m_hasLastByte = false;
}

QList<Packetizer::Packet> Packetizer::feed(const QByteArray &data, qint64 monotonicNs)
{
    QList<Packet> packets;
    if (data.isEmpty())
        return packets;

    switch (m_mode) {
    case Mode::Stream:
        // Сквозной режим: пакетизатор ничего не решает, границы найдёт разборщик по
        // переводам строк.
        packets.append(Packet{data, false});
        break;

    case Mode::InterByteTimeout: {
        // Пауза, разделившая эту порцию и предыдущую, завершает накопленный пакет.
        // Проверка идёт до накопления: данные, пришедшие после паузы, принадлежат уже
        // следующему сообщению.
        const qint64 gapNs = monotonicNs - m_lastByteNs;
        if (!m_pending.isEmpty() && m_hasLastByte
            && gapNs > qint64(m_timeoutMs) * 1'000'000) {
            packets.append(Packet{m_pending, true});
            m_pending.clear();
        }
        m_pending.append(data);
        m_lastByteNs = monotonicNs;
        m_hasLastByte = true;
        break;
    }

    case Mode::Delimiter: {
        if (m_delimiter.isEmpty()) {
            packets.append(Packet{data, false});
            break;
        }
        m_pending.append(data);
        // packetStart — начало ещё не выданного пакета. Отсчёт именно от него, а не от
        // начала накопителя: в одной порции разделителей может быть несколько, и каждый
        // следующий пакет начинается там, где кончился предыдущий.
        qsizetype packetStart = 0;
        for (;;) {
            const qsizetype at = m_pending.indexOf(m_delimiter, packetStart);
            if (at < 0)
                break;
            // Разделитель включается в пакет: для монитора порта важно видеть всё, что
            // реально пришло, а не очищенную версию.
            const qsizetype end = at + m_delimiter.size();
            packets.append(Packet{m_pending.mid(packetStart, end - packetStart), true});
            packetStart = end;
        }
        if (packetStart > 0)
            m_pending.remove(0, packetStart);
        break;
    }

    case Mode::FixedLength:
        m_pending.append(data);
        while (m_pending.size() >= m_fixedLength) {
            packets.append(Packet{m_pending.left(m_fixedLength), true});
            m_pending.remove(0, m_fixedLength);
        }
        break;
    }

    return packets;
}

Packetizer::Packet Packetizer::flush()
{
    if (m_pending.isEmpty())
        return {};

    Packet packet{m_pending, true};
    m_pending.clear();
    return packet;
}

} // namespace spotty
