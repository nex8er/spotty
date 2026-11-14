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

    if (m_mode == Mode::Stream || (m_mode == Mode::Delimiter && m_delimiter.isEmpty())) {
        packets.append(Packet{data, false});
        return packets;
    }

    if (m_mode == Mode::InterByteTimeout) {
        // Пауза, разделившая эту порцию и предыдущую, завершает накопленный пакет.
        // Проверка идёт до накопления: данные, пришедшие после паузы, принадлежат уже
        // следующему сообщению.
        const qint64 gapNs = monotonicNs - m_lastByteNs;
        if (!m_pending.isEmpty() && m_hasLastByte
            && gapNs > qint64(m_timeoutMs) * 1'000'000) {
            packets.append(Packet{m_pending, true});
            m_pending.clear();
        }
        m_lastByteNs = monotonicNs;
        m_hasLastByte = true;
    }

    m_pending.append(data);
    qsizetype packetStart = 0;
    qsizetype newline = m_pending.indexOf('\n');
    qsizetype delimiter = m_mode == Mode::Delimiter ? m_pending.indexOf(m_delimiter) : -1;

    for (;;) {
        qsizetype end = -1;
        if (delimiter >= 0)
            end = delimiter + m_delimiter.size();
        if (m_mode == Mode::FixedLength && m_pending.size() - packetStart >= m_fixedLength)
            end = packetStart + m_fixedLength;

        // \brief Перевод строки завершает накопление раньше дополнительного ограничения.
        if (newline >= 0 && (end < 0 || newline + 1 <= end))
            end = newline + 1;
        if (end < 0)
            break;

        // \brief Завершённый пакет из одних управляющих байтов тоже нужен HEX-режиму.
        packets.append(Packet{m_pending.mid(packetStart, end - packetStart), true});
        packetStart = end;
        if (newline >= 0 && newline < packetStart)
            newline = m_pending.indexOf('\n', packetStart);
        if (delimiter >= 0 && delimiter < packetStart)
            delimiter = m_pending.indexOf(m_delimiter, packetStart);
    }
    if (packetStart > 0)
        m_pending.remove(0, packetStart);

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
