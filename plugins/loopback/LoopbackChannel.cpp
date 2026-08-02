/**
 * \file LoopbackChannel.cpp
 * \brief Реализация spotty::LoopbackChannel.
 */
#include "LoopbackChannel.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>

namespace spotty {

namespace {

// Ключи настроек; должны совпадать со схемой в LoopbackPlugin::settingsSchema().
constexpr auto kMode = "mode";
constexpr auto kEchoDelay = "echoDelayMs";
constexpr auto kChatterInterval = "chatterIntervalMs";
constexpr auto kAnsi = "ansiColors";

/**
 * \brief Текущее значение монотонных часов в наносекундах.
 *
 * Именно монотонных, а не системных: spotty::IInterfaceChannel::dataReceived() требует
 * отметку, из которой считаются межбайтовые паузы. Перевод системного времени или
 * синхронизация по NTP дали бы отрицательные интервалы и сломали бы пакетизацию.
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

} // namespace

LoopbackChannel::LoopbackChannel(QObject *parent)
    : IInterfaceChannel(parent)
{
}

bool LoopbackChannel::open(const QVariantMap &settings, QString *error)
{
    Q_UNUSED(error); // Виртуальному каналу нечему помешать открыться.

    m_settings = settings;
    m_counter = 0;
    setState(ChannelState::Open, tr("Virtual channel"));

    // Таймер создаётся здесь, а не в конструкторе: к моменту open() объект уже перенесён
    // в поток ввода-вывода, и таймер должен принадлежать именно ему.
    const int interval = m_settings.value(QLatin1String(kChatterInterval)).toInt();
    if (m_settings.value(QLatin1String(kMode)).toString() == QLatin1String("chatter")
        && interval > 0) {
        m_chatterTimer = new QTimer(this);
        m_chatterTimer->setInterval(interval);
        connect(m_chatterTimer, &QTimer::timeout, this, &LoopbackChannel::emitChatterLine);
        m_chatterTimer->start();
    }

    return true;
}

void LoopbackChannel::close()
{
    if (m_chatterTimer) {
        m_chatterTimer->stop();
        delete m_chatterTimer;
        m_chatterTimer = nullptr;
    }
    setState(ChannelState::Closed);
}

bool LoopbackChannel::applySettings(const QVariantMap &settings)
{
    if (m_state != ChannelState::Open)
        return false;

    close();
    QString error;
    return open(settings, &error);
}

qint64 LoopbackChannel::write(const QByteArray &data)
{
    if (m_state != ChannelState::Open)
        return -1;

    if (m_settings.value(QLatin1String(kMode)).toString() == QLatin1String("echo")) {
        const int delay = qMax(0, m_settings.value(QLatin1String(kEchoDelay)).toInt());
        QTimer::singleShot(delay, this, [this, data] {
            // Проверка состояния обязательна: за время задержки канал могли закрыть.
            if (m_state == ChannelState::Open)
                Q_EMIT dataReceived(data, monotonicNow());
        });
    }

    return data.size();
}

void LoopbackChannel::emitChatterLine()
{
    ++m_counter;

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    const QByteArray line =
        QStringLiteral("[%1] frame %2 rssi=%3 dBm\r\n")
            .arg(timestamp)
            .arg(m_counter, 6, 10, QLatin1Char('0'))
            .arg(-40 - int(m_counter % 50))
            .toUtf8();

    Q_EMIT dataReceived(decorate(line), monotonicNow());

    // Каждая десятая строка — другого вида и другого цвета. Так при разработке терминала
    // регулярно проверяется смена атрибутов SGR посреди потока, а не только один цвет.
    if (m_counter % 10 == 0) {
        const QByteArray warning =
            QByteArrayLiteral("\x1b[33mWARN\x1b[0m  buffer at ")
            + QByteArray::number(int(m_counter % 100)) + QByteArrayLiteral("%\r\n");
        Q_EMIT dataReceived(m_settings.value(QLatin1String(kAnsi)).toBool()
                                ? warning
                                : QByteArrayLiteral("WARN  buffer\r\n"),
                            monotonicNow());
    }
}

QByteArray LoopbackChannel::decorate(const QByteArray &text) const
{
    if (!m_settings.value(QLatin1String(kAnsi)).toBool())
        return text;
    return QByteArrayLiteral("\x1b[36m") + text + QByteArrayLiteral("\x1b[0m");
}

QVariantMap LoopbackChannel::controlLines() const
{
    return {
        {QStringLiteral("CTS"), m_rts},
        {QStringLiteral("DSR"), m_dtr},
        {QStringLiteral("DCD"), m_dtr},
        {QStringLiteral("RI"), false},
    };
}

bool LoopbackChannel::setControlLine(const QString &name, bool asserted)
{
    if (name == QLatin1String("DTR"))
        m_dtr = asserted;
    else if (name == QLatin1String("RTS"))
        m_rts = asserted;
    else
        return false;

    Q_EMIT controlLinesChanged();
    return true;
}

void LoopbackChannel::setState(ChannelState state, const QString &detail)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(m_state, detail);
}

} // namespace spotty
