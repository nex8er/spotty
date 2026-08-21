/**
 * \file JsonGenChannel.cpp
 * \brief Реализация spotty::JsonGenChannel.
 */
#include "JsonGenChannel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTimer>

#include <cmath>

namespace spotty {

namespace {

// Ключи настроек; должны совпадать со схемой в JsonGenPlugin::settingsSchema().
constexpr auto kShape = "shape";
constexpr auto kFieldCount = "fieldCount";
constexpr auto kDepth = "depth";
constexpr auto kArraySize = "arraySize";
constexpr auto kUuidIds = "uuidIds";
constexpr auto kInterval = "intervalMs";
constexpr auto kMixedRates = "mixedRates";
constexpr auto kCorrupt = "corruptPercent";
constexpr auto kLogLines = "logLines";
constexpr auto kLayout = "layout";
constexpr auto kSplitPackets = "splitPackets";
constexpr auto kDriftMode = "driftMode";
constexpr auto kDriftEvery = "driftEveryDocs";

/// \brief Через сколько документов проскакивает текстовая строка.
constexpr quint64 kLogEveryDocs = 7;

/// \name Периодичность групп полей в режиме разных частот
/// @{
constexpr quint64 kMediumEvery = 5;
constexpr quint64 kSlowEvery = 25;
/// @}

/// \brief Имена полей; берутся по кругу, поэтому список короче предела в 64 поля.
constexpr const char *kFieldNames[] = {
    "temp", "humidity", "voltage", "current", "rpm", "pressure",
    "altitude", "heading", "battery", "signal", "errors", "uptime",
};

/**
 * \brief Текущее значение монотонных часов в наносекундах.
 *
 * Монотонных, а не системных: из этой отметки ядро считает межбайтовые паузы для
 * пакетизации. Тот же приём, что в spotty::SignalGenChannel.
 */
qint64 monotonicNow()
{
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.nsecsElapsed();
}

/// \brief Значение поля: синус со своим периодом, чтобы поля не менялись синхронно.
double fieldValue(int field, double t)
{
    const double period = 2.0 + field * 1.7;
    return std::round(100.0 * std::sin(2.0 * M_PI * t / period)) / 10.0;
}

} // namespace

JsonGenChannel::JsonGenChannel(QObject *parent)
    : IInterfaceChannel(parent)
{
}

bool JsonGenChannel::open(const QVariantMap &settings, QString *error)
{
    Q_UNUSED(error); // Виртуальному источнику нечему помешать открыться.

    m_settings = settings;
    m_counter = 0;
    m_clock.start();
    setState(ChannelState::Open, tr("Virtual JSON source"));

    // Таймер создаётся здесь, а не в конструкторе: к моменту open() объект уже перенесён в
    // поток ввода-вывода, и таймер должен принадлежать именно ему.
    m_timer = new QTimer(this);
    m_timer->setInterval(qMax(1, m_settings.value(QLatin1String(kInterval)).toInt()));
    connect(m_timer, &QTimer::timeout, this, &JsonGenChannel::emitDocument);
    m_timer->start();

    return true;
}

void JsonGenChannel::close()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    setState(ChannelState::Closed);
}

qint64 JsonGenChannel::write(const QByteArray &data)
{
    return m_state == ChannelState::Open ? data.size() : -1;
}

bool JsonGenChannel::applySettings(const QVariantMap &settings)
{
    if (m_state != ChannelState::Open)
        return false;

    // Перезапуск, а не перенастройка на лету: счётчик документов, от которого считаются и
    // частоты полей, и дрейф структуры, начинает заново — предсказуемее для отладки.
    close();
    QString error;
    return open(settings, &error);
}

// --- Построение документа ----------------------------------------------------------------

bool JsonGenChannel::fieldIsDue(int field) const
{
    if (!m_settings.value(QLatin1String(kMixedRates)).toBool())
        return true;

    switch (field % 3) {
    case 0:
        return true; // Быстрое: приходит с каждым документом.
    case 1:
        return m_counter % kMediumEvery == 0;
    default:
        return m_counter % kSlowEvery == 0;
    }
}

QString JsonGenChannel::fieldName(int field) const
{
    constexpr int kNameCount = int(std::size(kFieldNames));
    const QString base = QLatin1String(kFieldNames[field % kNameCount]);
    // Поля сверх списка имён получают номер, иначе они слились бы в один путь.
    return field < kNameCount ? base : base + QString::number(field / kNameCount);
}

QJsonObject JsonGenChannel::flatObject(double t) const
{
    const int fields = qMax(1, m_settings.value(QLatin1String(kFieldCount)).toInt());
    const QString drift = m_settings.value(QLatin1String(kDriftMode)).toString();
    const quint64 every = qMax(1ULL, m_settings.value(QLatin1String(kDriftEvery)).toULongLong());

    QJsonObject object;
    for (int i = 0; i < fields; ++i) {
        if (!fieldIsDue(i))
            continue;
        object.insert(fieldName(i), fieldValue(i, t));
    }

    if (drift == QLatin1String("grow")) {
        // Новые ключи копятся: дерево обязано расти вместе с потоком.
        const int extra = int(m_counter / every);
        for (int i = 0; i < extra; ++i)
            object.insert(QStringLiteral("extra%1").arg(i), double(i));
    } else if (drift == QLatin1String("cycle")) {
        // Ключ живёт один отрезок и пропадает: замолчавшее поле обязано быть видно.
        object.insert(QStringLiteral("phase%1").arg(m_counter / every), double(m_counter));
    }

    // Значения разных типов в каждом документе: разбор обязан показать их все.
    object.insert(QStringLiteral("ok"), (m_counter / 4) % 2 == 0);
    object.insert(QStringLiteral("state"), QStringLiteral("running"));
    object.insert(QStringLiteral("fault"), QJsonValue());
    object.insert(QStringLiteral("rgb"),
                  QJsonArray{int(m_counter % 256), 128, 64});
    object.insert(QStringLiteral("seq"), double(m_counter));
    return object;
}

QJsonObject JsonGenChannel::nestedObject(double t) const
{
    const int depth = qBound(1, m_settings.value(QLatin1String(kDepth)).toInt(), 8);

    // Собирается изнутри наружу: так глубина получается ровно заказанной, без рекурсии.
    QJsonObject inner = flatObject(t);
    for (int level = depth - 1; level > 0; --level) {
        QJsonObject wrapper;
        wrapper.insert(QStringLiteral("level%1").arg(level), inner);
        wrapper.insert(QStringLiteral("depth"), double(level));
        inner = wrapper;
    }

    QJsonObject root;
    root.insert(QStringLiteral("device"), QStringLiteral("jsongen"));
    root.insert(QStringLiteral("sensors"), inner);
    return root;
}

QJsonArray JsonGenChannel::objectArray(double t) const
{
    const int count = qMax(1, m_settings.value(QLatin1String(kArraySize)).toInt());
    const bool uuid = m_settings.value(QLatin1String(kUuidIds)).toBool();

    QJsonArray array;
    for (int i = 0; i < count; ++i) {
        QJsonObject element;
        // Со случайными идентификаторами ни один элемент не встречается дважды, и дерево
        // растёт без предела — ровно то, ради чего предел узлов и заведён.
        element.insert(QStringLiteral("id"),
                       uuid ? double(QRandomGenerator::global()->bounded(1000000))
                            : double(i + 1));
        element.insert(QStringLiteral("v"), fieldValue(i, t));
        element.insert(QStringLiteral("ok"), (m_counter + quint64(i)) % 3 != 0);
        array.append(element);
    }
    return array;
}

// --- Выдача ------------------------------------------------------------------------------

QString JsonGenChannel::corrupt(const QString &text) const
{
    const int percent = m_settings.value(QLatin1String(kCorrupt)).toInt();
    if (percent <= 0 || int(QRandomGenerator::global()->bounded(100)) >= percent)
        return text;

    switch (QRandomGenerator::global()->bounded(3)) {
    case 0:
        // Обрубленный хвост — самое частое следствие переполнения буфера на устройстве.
        return text.left(qMax(1, text.size() / 2));
    case 1:
        // Потерянная закрывающая скобка: разбор обязан бросить её по таймауту, а не
        // проглотить весь последующий поток.
        return text.left(text.size() - 1);
    default: {
        // Лишняя запятая: скобки сходятся, а документ невалиден.
        QString broken = text;
        broken.insert(qMax(1, broken.size() - 1), u',');
        return broken;
    }
    }
}

void JsonGenChannel::emitText(const QString &text)
{
    const QByteArray payload = (text + QStringLiteral("\r\n")).toUtf8();

    if (!m_settings.value(QLatin1String(kSplitPackets)).toBool() || payload.size() < 4) {
        Q_EMIT dataReceived(payload, monotonicNow());
        return;
    }

    // Разрыв посреди строки: читатель обязан дождаться её конца, а не разбирать половину.
    const int cut = payload.size() / 2;
    Q_EMIT dataReceived(payload.left(cut), monotonicNow());
    Q_EMIT dataReceived(payload.mid(cut), monotonicNow());
}

void JsonGenChannel::emitDocument()
{
    const double t = m_clock.nsecsElapsed() / 1'000'000'000.0;
    const QString shape = m_settings.value(QLatin1String(kShape)).toString();

    QJsonDocument document;
    QString effective = shape;
    if (shape == QLatin1String("mixed")) {
        // Чередование форм — случай, на котором разбор ошибается чаще всего.
        constexpr const char *kOrder[] = {"flat", "nested", "array"};
        effective = QLatin1String(kOrder[m_counter % 3]);
    }

    if (effective == QLatin1String("nested"))
        document = QJsonDocument(nestedObject(t));
    else if (effective == QLatin1String("array"))
        document = QJsonDocument(objectArray(t));
    else
        document = QJsonDocument(flatObject(t));

    const QString layout = m_settings.value(QLatin1String(kLayout)).toString();
    bool pretty = layout == QLatin1String("pretty");
    if (layout == QLatin1String("alternate"))
        pretty = (m_counter % 2) == 1;

    const QString text = QString::fromUtf8(
        document.toJson(pretty ? QJsonDocument::Indented : QJsonDocument::Compact)).trimmed();

    emitText(corrupt(text));

    if (m_settings.value(QLatin1String(kLogLines)).toBool()
        && m_counter % kLogEveryDocs == 0) {
        // Обычный текст вперемешку с телеметрией — так ведёт себя настоящая прошивка.
        emitText(QStringLiteral("[%1] jsongen: %2 documents emitted")
                     .arg(t, 9, 'f', 3)
                     .arg(m_counter));
    }

    ++m_counter;
}

void JsonGenChannel::setState(ChannelState state, const QString &detail)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(m_state, detail);
}

} // namespace spotty
