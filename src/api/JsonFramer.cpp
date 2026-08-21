/**
 * \file JsonFramer.cpp
 * \brief Реализация spotty::JsonFramer.
 */
#include <spotty/data/JsonFramer.h>

#include <QJsonParseError>

namespace spotty {

namespace {

/// \brief Миллисекунда в наносекундах — для сравнения таймаута с монотонной отметкой.
constexpr qint64 kMsToNs = 1'000'000;

} // namespace

std::optional<QJsonDocument> JsonFramer::parseDocument(const QString &text)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        return std::nullopt;
    // Скаляр отвергается намеренно: «42» в логе устройства — это число в сообщении, а не
    // телеметрия. См. \par «Что не является документом» в заголовке.
    if (!doc.isObject() && !doc.isArray())
        return std::nullopt;
    return doc;
}

void JsonFramer::scanBalance(const QString &line)
{
    for (const QChar ch : line) {
        if (m_inString) {
            if (m_escaped) {
                m_escaped = false;
            } else if (ch == u'\\') {
                m_escaped = true;
            } else if (ch == u'"') {
                m_inString = false;
            }
            continue;
        }

        if (ch == u'"')
            m_inString = true;
        else if (ch == u'{' || ch == u'[')
            ++m_depth;
        else if (ch == u'}' || ch == u']')
            --m_depth;
    }
}

void JsonFramer::beginPending(const QString &line, qint64 monotonicNs)
{
    m_pending = line;
    m_pendingLines = 1;
    m_pendingStartNs = monotonicNs;
    m_depth = 0;
    m_inString = false;
    m_escaped = false;
    scanBalance(line);
}

void JsonFramer::abandonPending()
{
    ++m_counters.abandoned;
    reset();
}

void JsonFramer::reset()
{
    m_pending.clear();
    m_pendingLines = 0;
    m_pendingStartNs = 0;
    m_depth = 0;
    m_inString = false;
    m_escaped = false;
}

void JsonFramer::setMaxPendingLines(int lines)
{
    m_maxPendingLines = qMax(2, lines);
}

void JsonFramer::setPendingTimeoutMs(int milliseconds)
{
    m_pendingTimeoutMs = qMax(0, milliseconds);
}

std::optional<QJsonDocument> JsonFramer::feed(const QString &rawLine, qint64 monotonicNs)
{
    const QString line = rawLine.trimmed();

    // Истёкшее накопление бросается до разбора строки, а не после: строка, ради которой
    // истечение и заметили, обязана быть разобрана как новая, а не дописана к мусору.
    if (isPending() && m_pendingTimeoutMs > 0
        && monotonicNs - m_pendingStartNs > qint64(m_pendingTimeoutMs) * kMsToNs) {
        abandonPending();
    }

    if (!isPending()) {
        if (line.isEmpty())
            return std::nullopt;

        // NDJSON — самый частый случай, поэтому пробуется первым.
        if (auto doc = parseDocument(line)) {
            ++m_counters.documents;
            return doc;
        }

        if (line.startsWith(u'{') || line.startsWith(u'[')) {
            beginPending(line, monotonicNs);
            // Скобки уже сошлись, а разбор выше не удался: строка вроде `{"a":}` —
            // сбалансированный мусор. Копить дальше нечего.
            if (m_depth <= 0) {
                ++m_counters.malformed;
                reset();
            }
            return std::nullopt;
        }

        ++m_counters.textLines;
        return std::nullopt;
    }

    // Перевод строки нужен ровно затем, чтобы `//`-мусора в буфере не слиплось в одну
    // строку; на разбор JSON он не влияет.
    m_pending += u'\n';
    m_pending += line;
    ++m_pendingLines;
    scanBalance(line);

    if (m_pendingLines > m_maxPendingLines || m_pending.size() > kMaxPendingBytes) {
        abandonPending();
        return std::nullopt;
    }

    if (m_depth > 0)
        return std::nullopt;

    const QString complete = m_pending;
    reset();

    if (auto doc = parseDocument(complete)) {
        ++m_counters.documents;
        return doc;
    }
    ++m_counters.malformed;
    return std::nullopt;
}

} // namespace spotty
