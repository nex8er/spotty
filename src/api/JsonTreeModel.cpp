/**
 * \file JsonTreeModel.cpp
 * \brief Реализация spotty::JsonTreeModel.
 */
#include <spotty/data/JsonTreeModel.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <cmath>
#include <functional>

namespace spotty {

namespace {

/**
 * \brief Вес нового интервала в сглаживании.
 *
 * Четверть: на первых отсчётах число садится быстро, дальше дрожание отдельного интервала
 * почти не двигает показанную частоту. Меньше — реакция на смену темпа становится вялой,
 * больше — цифра пляшет на каждой строке.
 */
constexpr double kIntervalAlpha = 0.25;

/// \brief Наименьший учитываемый интервал: два документа в одну наносекунду дали бы бесконечность.
constexpr qint64 kMinIntervalNs = 1000;

/// \brief Как часто пересчитывается наибольшая частота.
constexpr qint64 kMaxRateCacheNs = 200'000'000;

/// \brief Ниже этого порога поле считается замершим независимо от своего интервала.
constexpr qint64 kStaleFloorNs = 3'000'000'000;

/// \brief Во столько раз простой должен превысить обычный интервал, чтобы поле сочли замершим.
constexpr int kStaleFactor = 20;

/// \brief Предел длины свёрнутого значения: дальше строка всё равно не читается.
constexpr int kCompactLimit = 120;

/// \brief Знаков после запятой у дробного числа.
constexpr int kNumberPrecision = 6;

/// \brief Наибольшее целое, представимое double точно.
constexpr double kExactIntegerLimit = 9007199254740992.0; // 2^53

} // namespace

JsonTreeModel::JsonTreeModel(QObject *parent)
    : QObject(parent)
{
    // Корень заводится сразу и живёт всегда: он не узел данных, а точка входа обхода.
    m_nodes.append(JsonNode{});
}

QString JsonTreeModel::noIdentityName()
{
    return QStringLiteral("(no id)");
}

bool JsonTreeModel::isValidNode(int index) const
{
    return index > 0 && index < m_nodes.size() && m_nodes.at(index).alive;
}

QString JsonTreeModel::path(int index) const
{
    if (!isValidNode(index))
        return {};

    QStringList parts;
    for (int at = index; at > 0; at = m_nodes.at(at).parent)
        parts.prepend(m_nodes.at(at).name);
    return parts.join(u'.');
}

// --- Частота ---------------------------------------------------------------------------

double JsonTreeModel::rate(int index, qint64 nowNs) const
{
    if (!isValidNode(index))
        return 0.0;

    const JsonNode &n = m_nodes.at(index);
    // Одного прихода мало: интервала ещё не существует, и любое число было бы выдумкой.
    if (n.updates < 2 || n.avgIntervalNs <= 0.0)
        return 0.0;

    const double idle = double(qMax<qint64>(nowNs - n.lastUpdateNs, 0));
    // Здесь вся суть затухания: пока данные идут, простой меньше среднего интервала и
    // ничего не меняет; как только поток встал — знаменатель растёт вместе с простоем.
    return 1e9 / qMax(n.avgIntervalNs, idle);
}

double JsonTreeModel::maxRate(qint64 nowNs) const
{
    if (m_maxRateValid && qAbs(nowNs - m_maxRateAtNs) < kMaxRateCacheNs)
        return m_maxRate;

    double top = 0.0;
    for (int i = 1; i < m_nodes.size(); ++i) {
        if (!m_nodes.at(i).alive)
            continue;
        top = qMax(top, rate(i, nowNs));
    }

    m_maxRate = top;
    m_maxRateAtNs = nowNs;
    m_maxRateValid = true;
    return top;
}

bool JsonTreeModel::isStale(int index, qint64 nowNs) const
{
    if (!isValidNode(index))
        return false;

    const JsonNode &n = m_nodes.at(index);
    if (n.updates == 0)
        return true;

    const qint64 idle = nowNs - n.lastUpdateNs;
    const qint64 threshold =
        qMax(kStaleFloorNs, qint64(n.avgIntervalNs) * kStaleFactor);
    return idle > threshold;
}

qint64 JsonTreeModel::nsSinceChange(int index, qint64 nowNs) const
{
    if (!isValidNode(index))
        return 0;
    return qMax<qint64>(nowNs - m_nodes.at(index).lastChangeNs, 0);
}

// --- Настройки -------------------------------------------------------------------------

void JsonTreeModel::setIdentityKey(const QString &key)
{
    const QString trimmed = key.trimmed();
    if (m_identityKey == trimmed)
        return;
    m_identityKey = trimmed;
    // Прежнее дерево построено по другому правилу: в нём ветки массива либо схлопнуты, либо
    // разведены по старому ключу. Пересобрать одно в другое нельзя — только заново.
    clear();
}

void JsonTreeModel::setMaxNodes(int nodes)
{
    m_maxNodes = qBound(1, nodes, 200000);
}

void JsonTreeModel::setMaxDepth(int depth)
{
    m_maxDepth = qBound(1, depth, 64);
}

void JsonTreeModel::setMaxChildren(int children)
{
    m_maxChildren = qBound(1, children, 100000);
}

// --- Арена -----------------------------------------------------------------------------

int JsonTreeModel::allocateNode()
{
    if (!m_freeSlots.isEmpty()) {
        const int index = m_freeSlots.takeLast();
        m_nodes[index] = JsonNode{};
        return index;
    }
    m_nodes.append(JsonNode{});
    return int(m_nodes.size()) - 1;
}

int JsonTreeModel::ensureChild(int parent, const QString &name, JsonNodeKind kind, int depth)
{
    {
        const JsonNode &p = m_nodes.at(parent);
        const auto it = p.childIndex.constFind(name);
        if (it != p.childIndex.constEnd())
            return *it;
    }

    if (m_liveNodes >= m_maxNodes || m_nodes.at(parent).children.size() >= m_maxChildren) {
        // Отвергнутые пути не запоминаются: множество отказов росло бы само по себе, ради
        // чего предел и заводился. Считается только их число.
        ++m_rejected;
        m_truncated = true;
        return -1;
    }

    // Слот берётся до записи в родителя: allocateNode() может перевыделить арену, и ссылка
    // на родителя, взятая раньше, повисла бы. Здесь и ниже — только обращения по индексу.
    const int index = allocateNode();
    m_nodes[index].name = name;
    m_nodes[index].parent = parent;
    m_nodes[index].kind = kind;
    m_nodes[index].depth = depth;

    m_nodes[parent].children.append(index);
    m_nodes[parent].childIndex.insert(name, index);

    ++m_liveNodes;
    m_added = true;
    return index;
}

void JsonTreeModel::touch(int index, qint64 monotonicNs)
{
    JsonNode &n = m_nodes[index];
    // Один документ — одно обновление пути. Иначе схлопнутый массив из десяти элементов
    // накрутил бы полю десятикратную частоту, которой в потоке нет.
    if (n.epoch == m_epoch)
        return;
    n.epoch = m_epoch;

    if (n.updates == 0) {
        n.lastUpdateNs = monotonicNs;
        n.lastChangeNs = monotonicNs;
    } else {
        const qint64 dt = qMax(monotonicNs - n.lastUpdateNs, kMinIntervalNs);
        n.avgIntervalNs = (n.updates == 1)
                              ? double(dt)
                              : (1.0 - kIntervalAlpha) * n.avgIntervalNs
                                  + kIntervalAlpha * double(dt);
        n.lastUpdateNs = monotonicNs;
    }
    ++n.updates;
    m_maxRateValid = false;
}

void JsonTreeModel::setNodeValue(int index, const QString &text, qint64 monotonicNs)
{
    JsonNode &n = m_nodes[index];
    if (n.value == text)
        return;

    // Отметка двигается только на смене текста: поле, честно рапортующее одно и то же сто
    // раз в секунду, иначе превратилось бы в стробоскоп.
    n.value = text;
    n.lastChangeNs = monotonicNs;
    m_dirty.append(index);
}

// --- Разбор ----------------------------------------------------------------------------

JsonNodeKind JsonTreeModel::kindOf(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool:   return JsonNodeKind::Bool;
    case QJsonValue::Double: return JsonNodeKind::Number;
    case QJsonValue::String: return JsonNodeKind::String;
    case QJsonValue::Array:  return JsonNodeKind::Array;
    case QJsonValue::Object: return JsonNodeKind::Object;
    default:                 return JsonNodeKind::Null;
    }
}

QString JsonTreeModel::formatScalar(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double number = value.toDouble();
        // Целое печатается целым: «12.000000» в колонке значений нечитаемо, а разрядность
        // double за пределами 2^53 всё равно перестаёт быть целочисленной.
        if (std::isfinite(number) && std::abs(number) < kExactIntegerLimit
            && number == std::floor(number)) {
            return QString::number(qint64(number));
        }
        return QString::number(number, 'g', kNumberPrecision);
    }
    case QJsonValue::String: {
        const QString text = value.toString();
        // Кавычки ставятся ровно там, где без них возникает двусмысленность: строка «12» и
        // число 12 обязаны различаться на экране. Везде подряд они были бы шумом.
        bool numeric = false;
        text.toDouble(&numeric);
        if (numeric || text == QLatin1String("true") || text == QLatin1String("false")
            || text == QLatin1String("null")) {
            return u'"' + text + u'"';
        }
        return text;
    }
    case QJsonValue::Null:
        return QStringLiteral("null");
    default:
        return {};
    }
}

QString JsonTreeModel::compactText(const QJsonValue &value)
{
    QJsonDocument doc;
    if (value.isObject())
        doc = QJsonDocument(value.toObject());
    else if (value.isArray())
        doc = QJsonDocument(value.toArray());
    else
        return formatScalar(value);

    const QString text = QString::fromUtf8(doc.toJson(QJsonDocument::Compact)).trimmed();
    if (text.size() <= kCompactLimit)
        return text;
    return text.left(kCompactLimit - 1) + QChar(0x2026); // …
}

bool JsonTreeModel::isScalarArray(const QJsonArray &array)
{
    for (const QJsonValue value : array) {
        if (value.isObject() || value.isArray())
            return false;
    }
    return true;
}

bool JsonTreeModel::feed(const QJsonDocument &document, qint64 monotonicNs)
{
    if (!document.isObject() && !document.isArray())
        return false;

    ++m_epoch;
    ++m_documents;
    m_added = false;

    if (document.isObject()) {
        mergeObject(0, document.object(), monotonicNs, 1);
    } else {
        const QJsonArray array = document.array();
        // Массив на верхнем уровне вливается в корень так же, как вложенный — в свою ветку.
        mergeArray(0, array, monotonicNs, 1);
    }

    if (m_added)
        Q_EMIT nodesAdded();
    Q_EMIT changed();
    return true;
}

void JsonTreeModel::mergeObject(int parent, const QJsonObject &object, qint64 ns, int depth)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        mergeValue(parent, it.key(), it.value(), ns, depth);
}

void JsonTreeModel::mergeValue(int parent, const QString &name, const QJsonValue &value,
                               qint64 ns, int depth)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const int node = ensureChild(parent, name, JsonNodeKind::Object, depth);
        if (node < 0)
            return;
        touch(node, ns);
        // Опустошение — это событие. Если бы пустой объект не давал строки, исчезновение
        // содержимого выглядело бы как пропажа поля, то есть как ошибка панели.
        if (object.isEmpty()) {
            setNodeValue(node, QStringLiteral("{}"), ns);
        } else if (depth >= m_maxDepth) {
            // Предел считается по узлам, а не по вызовам: глубже него узлы не создаются
            // вовсе, а последний разрешённый показывает поддерево свёрнутым. Иначе
            // «заглушка» сама оказалась бы на запрещённой глубине.
            setNodeValue(node, compactText(value), ns);
        } else {
            mergeObject(node, object, ns, depth + 1);
        }
        return;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();

        // Массив скаляров — одно понятие: RGB, кватернион, три оси акселерометра. Разложить
        // его по индексам значило бы разобрать понятие на буквы. К тому же индекс не
        // является именем: стоит массиву поменять длину, и значения переезжают между
        // ветками, а частота каждой становится ложью.
        if (array.isEmpty() || isScalarArray(array)) {
            const int node = ensureChild(parent, name, JsonNodeKind::Compact, depth);
            if (node < 0)
                return;
            touch(node, ns);
            setNodeValue(node, array.isEmpty() ? QStringLiteral("[]") : compactText(value), ns);
            return;
        }

        const int node = ensureChild(parent, name, JsonNodeKind::Array, depth);
        if (node < 0)
            return;
        touch(node, ns);
        if (depth >= m_maxDepth)
            setNodeValue(node, compactText(value), ns);
        else
            mergeArray(node, array, ns, depth + 1);
        return;
    }

    const int node = ensureChild(parent, name, kindOf(value), depth);
    if (node < 0)
        return;
    touch(node, ns);
    setNodeValue(node, formatScalar(value), ns);
}

void JsonTreeModel::mergeArray(int node, const QJsonArray &array, qint64 ns, int depth)
{
    int index = 0;
    for (const QJsonValue value : array) {
        const int position = index++;

        if (!value.isObject()) {
            // Не объект внутри составного массива: различать его нечем, кроме позиции.
            mergeValue(node, u'[' + QString::number(position) + u']', value, ns, depth);
            continue;
        }

        const QJsonObject object = value.toObject();

        if (m_identityKey.isEmpty()) {
            // Схлопывание: элементы вливаются в саму ветку массива, и поле показывает
            // значение из последнего. Частоту это не накручивает — её держит эпоха.
            mergeObject(node, object, ns, depth);
            continue;
        }

        const QJsonValue identity = object.value(m_identityKey);
        const QString branchName =
            identity.isUndefined() ? noIdentityName() : formatScalar(identity);
        const int branch = ensureChild(node, branchName, JsonNodeKind::Object, depth);
        if (branch < 0)
            continue;
        touch(branch, ns);
        mergeObject(branch, object, ns, depth + 1);
    }
}

// --- Изменения и уборка ------------------------------------------------------------------

QList<int> JsonTreeModel::takeDirty()
{
    QList<int> out;
    out.swap(m_dirty);
    return out;
}

void JsonTreeModel::clear()
{
    m_nodes.clear();
    m_nodes.append(JsonNode{});
    m_freeSlots.clear();
    m_dirty.clear();
    m_liveNodes = 0;
    m_rejected = 0;
    m_truncated = false;
    m_maxRateValid = false;
    Q_EMIT modelReset();
}

void JsonTreeModel::releaseSubtree(int index, QList<int> *removed)
{
    const QList<int> children = m_nodes.at(index).children;
    for (const int child : children)
        releaseSubtree(child, removed);

    m_nodes[index] = JsonNode{};
    m_nodes[index].alive = false;
    m_freeSlots.append(index);
    --m_liveNodes;
    removed->append(index);
}

QList<int> JsonTreeModel::pruneStale(qint64 nowNs)
{
    QList<int> removed;

    // Обход снизу вверх: ветка удаляется только после того, как выяснилось, что от неё
    // ничего не осталось. Рекурсию разворачивать незачем — глубина ограничена m_maxDepth.
    const std::function<bool(int)> prune = [&](int index) -> bool {
        const QList<int> children = m_nodes.at(index).children;
        QList<int> survivors;
        for (const int child : children) {
            if (prune(child))
                survivors.append(child);
        }

        if (survivors.size() != children.size()) {
            m_nodes[index].children = survivors;
            QHash<QString, int> rebuilt;
            rebuilt.reserve(survivors.size());
            for (const int child : survivors)
                rebuilt.insert(m_nodes.at(child).name, child);
            m_nodes[index].childIndex = rebuilt;
        }

        if (index == 0)
            return true;
        // Ветка с выжившими детьми остаётся, даже если сама давно не приходила: без неё до
        // детей было бы не добраться.
        if (!survivors.isEmpty())
            return true;
        if (!isStale(index, nowNs))
            return true;

        releaseSubtree(index, &removed);
        return false;
    };

    prune(0);

    if (!removed.isEmpty()) {
        m_maxRateValid = false;
        Q_EMIT nodesRemoved(removed);
    }
    return removed;
}

} // namespace spotty
