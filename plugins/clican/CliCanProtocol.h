/**
 * \file CliCanProtocol.h
 * \brief Адресация и правила туннеля CLI поверх CAN.
 *
 * Здесь только арифметика и учёт — ни одного обращения к железу. Это сделано ради тестов:
 * протокол проверяется набором `spotty-tests` без адаптера, без драйвера PEAK и без
 * QApplication, а всё, что требует шины, живёт в spotty::CanBus и spotty::CliCanChannel.
 *
 * \see docs/CLICAN.md — исходное описание протокола.
 */
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>

#include <algorithm>

namespace spotty::clican {

/// \brief Начало выделенного туннелю диапазона идентификаторов.
inline constexpr quint32 kBroadcastId = 0x400;

/// \brief Последний идентификатор диапазона; вместе с 0x77E зарезервирован.
inline constexpr quint32 kLastReservedId = 0x77F;

/// \brief Идентификатор управляющих пакетов шлюзов.
inline constexpr quint32 kGatewayControlId = 0x401;

/// \brief Наименьший номер узла: нулевой зарезервирован под широковещание и шлюзы.
inline constexpr int kMinNode = 1;

/**
 * \brief Наибольший номер узла.
 *
 * \note Описание протокола называет 447, и одновременно объявляет 0x77E и 0x77F
 *       зарезервированными — а это ровно пара узла 447 (0x400 + 447*2 = 0x77E). Взято
 *       меньшее из двух: узел 447 адресовать нечем.
 */
inline constexpr int kMaxNode = 446;

/// \brief Больше восьми байт в кадр классического CAN не помещается.
inline constexpr int kMaxPayload = 8;

/**
 * \brief Через сколько молчания плата выходит из режима туннелирования.
 *
 * Значение стороны платы, а не наше: держать соединение живым нужно пакетами чаще этого.
 */
inline constexpr int kTunnelHoldMs = 5000;

/// \return `true`, если номер узла адресуем.
constexpr bool isValidNode(int node)
{
    return node >= kMinNode && node <= kMaxNode;
}

/// \brief Идентификатор, на который узлу шлют команды.
constexpr quint32 requestId(int node)
{
    return kBroadcastId + static_cast<quint32>(node) * 2;
}

/// \brief Идентификатор, с которого узел отвечает.
constexpr quint32 responseId(int node)
{
    return requestId(node) + 1;
}

/**
 * \brief Узел, которому принадлежит идентификатор ответа.
 * \return 0, если идентификатор не является ответом узла: чётный, вне диапазона или
 *         зарезервированный.
 *
 * Единственный способ узнать, кто есть на шине: узлы отвечают на широковещательный запрос
 * пустым пакетом со **своего** идентификатора ответа, и больше ничего о себе не сообщают.
 */
constexpr int nodeFromResponseId(quint32 id)
{
    if (id <= kGatewayControlId || id >= kLastReservedId)
        return 0;
    if ((id & 1u) == 0)
        return 0;
    const int node = static_cast<int>((id - kBroadcastId - 1) / 2);
    return isValidNode(node) ? node : 0;
}

/**
 * \brief Разобрать пакет управления потоком.
 * \param id Идентификатор пришедшего кадра.
 * \param payload Его данные.
 * \return Число миллисекунд паузы либо -1, если это не пакет управления потоком.
 *
 * Шлюз шлёт его, когда его очередь приёма подходит к концу: всем участникам сети следует
 * приостановить отправку, иначе шлюз начнёт терять кадры. Отличается от
 * широковещательного запроса присутствия ровно одним: длиной данных.
 */
inline int flowControlPauseMs(quint32 id, const QByteArray &payload)
{
    if (id != kBroadcastId || payload.size() != 1)
        return -1;
    return static_cast<quint8>(payload.at(0));
}

/**
 * \brief Нарезать поток байт на полезные нагрузки кадров.
 *
 * Границы кадров смысла не несут: на той стороне байты просто складываются в приёмный
 * буфер UART. Поэтому нарезка — по восемь, без выравнивания на строки.
 */
inline QList<QByteArray> splitPayload(const QByteArray &data)
{
    QList<QByteArray> result;
    result.reserve((data.size() + kMaxPayload - 1) / kMaxPayload);
    for (qsizetype offset = 0; offset < data.size(); offset += kMaxPayload)
        result.append(data.mid(offset, kMaxPayload));
    return result;
}

/**
 * \class NodeDirectory
 * \brief Узлы, ответившие на шине, с давностью последнего ответа.
 *
 * \par Почему с давностью
 *
 * Узел не сообщает об уходе — он просто перестаёт отвечать. Список, в который узлы только
 * добавляются, за час работы наберёт все платы, когда-либо включённые на стенде, и
 * перестанет отвечать на вопрос «что на шине сейчас». Устаревание отвечает на него
 * ценой одного числа на узел.
 *
 * Время передаётся снаружи (монотонные миллисекунды) — так класс проверяется тестом без
 * ожиданий в реальном времени.
 */
class NodeDirectory
{
public:
    /// \brief Отметить, что узел отозвался в момент \p nowMs.
    void noteSeen(int node, qint64 nowMs)
    {
        if (!isValidNode(node))
            return;
        m_lastSeen.insert(node, nowMs);
    }

    /**
     * \brief Узлы, отвечавшие не позже чем \p maxAgeMs назад, по возрастанию номера.
     * \param nowMs Текущий момент по тем же часам, что и в noteSeen().
     */
    QList<int> nodes(qint64 nowMs, qint64 maxAgeMs) const
    {
        QList<int> result;
        for (auto it = m_lastSeen.constBegin(); it != m_lastSeen.constEnd(); ++it) {
            if (nowMs - it.value() <= maxAgeMs)
                result.append(it.key());
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    /// \brief Забыть всё: шина закрыта или переоткрыта на другой скорости.
    void clear() { m_lastSeen.clear(); }

private:
    QHash<int, qint64> m_lastSeen;
};

} // namespace spotty::clican
