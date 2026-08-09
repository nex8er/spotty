/**
 * \file SignalGenChannel.cpp
 * \brief Реализация spotty::SignalGenChannel.
 */
#include "SignalGenChannel.h"

#include <QRandomGenerator>
#include <QStringList>
#include <QTimer>

#include <cmath>

namespace spotty {

namespace {

// Ключи настроек; должны совпадать со схемой в SignalGenPlugin::settingsSchema().
constexpr auto kWaveform = "waveform";
constexpr auto kPeriod = "periodMs";
constexpr auto kInterval = "sampleIntervalMs";
constexpr auto kAmplitude = "amplitude";
constexpr auto kOffset = "offset";
constexpr auto kNoise = "noisePercent";
constexpr auto kIncludeAxis = "includeAxis";
constexpr auto kIncludeMarkers = "includeMarkers";

// Раз в столько строк данных среди чисел проскакивает нечисловая: реальные устройства
// вперемешку с телеметрией шлют текст, и разбор обязан такую строку пропустить, а не
// сломаться (см. spotty::CsvSeries::feed()).
constexpr quint64 kMarkerEverySamples = 25;

/**
 * \brief Текущее значение монотонных часов в наносекундах.
 *
 * Именно монотонных, а не системных — как и в spotty::LoopbackChannel, из этой отметки
 * ядро считает межбайтовые паузы для пакетизации.
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

SignalGenChannel::SignalGenChannel(QObject *parent)
    : IInterfaceChannel(parent)
{
}

bool SignalGenChannel::open(const QVariantMap &settings, QString *error)
{
    Q_UNUSED(error); // Виртуальному источнику нечему помешать открыться.

    m_settings = settings;
    m_counter = 0;
    m_clock.start();
    setState(ChannelState::Open, tr("Virtual signal source"));

    // Таймер создаётся здесь, а не в конструкторе: к моменту open() объект уже перенесён в
    // поток ввода-вывода, и таймер должен принадлежать именно ему.
    const int interval = qMax(1, m_settings.value(QLatin1String(kInterval)).toInt());
    m_timer = new QTimer(this);
    m_timer->setInterval(interval);
    connect(m_timer, &QTimer::timeout, this, &SignalGenChannel::emitSample);
    m_timer->start();

    return true;
}

void SignalGenChannel::close()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    setState(ChannelState::Closed);
}

qint64 SignalGenChannel::write(const QByteArray &data)
{
    return m_state == ChannelState::Open ? data.size() : -1;
}

bool SignalGenChannel::applySettings(const QVariantMap &settings)
{
    if (m_state != ChannelState::Open)
        return false;

    // Перезапуск, а не перенастройка на лету: смена периода или режима посреди сигнала
    // всё равно дала бы скачок формы, а так ещё и #m_counter, а с ним счётчик режима
    // `growing`, начинает заново — предсказуемее для отладки.
    close();
    QString error;
    return open(settings, &error);
}

void SignalGenChannel::emitSample()
{
    ++m_counter;

    const double t = m_clock.nsecsElapsed() / 1'000'000'000.0;

    QStringList fields;
    if (m_settings.value(QLatin1String(kIncludeAxis)).toBool())
        fields << formatValue(t);
    fields += columnValues(t);

    Q_EMIT dataReceived((fields.join(u',') + QStringLiteral("\r\n")).toUtf8(), monotonicNow());

    if (m_settings.value(QLatin1String(kIncludeMarkers)).toBool()
        && m_counter % kMarkerEverySamples == 0) {
        const QByteArray marker =
            QStringLiteral("-- signalgen: %1 samples emitted waveform=%2 --\r\n")
                .arg(m_counter)
                .arg(m_settings.value(QLatin1String(kWaveform)).toString())
                .toUtf8();
        Q_EMIT dataReceived(marker, monotonicNow());
    }
}

QStringList SignalGenChannel::columnValues(double t) const
{
    const QString key = m_settings.value(QLatin1String(kWaveform)).toString();
    // Меньше 20 мс период не даёт ничему смысла: интервал отправки строк того же порядка,
    // и форма выродилась бы в чередование случайных отсчётов одной фазы.
    const double periodSec = qMax(0.02, m_settings.value(QLatin1String(kPeriod)).toInt() / 1000.0);
    const double amplitude = m_settings.value(QLatin1String(kAmplitude)).toInt();
    const double offset = m_settings.value(QLatin1String(kOffset)).toInt();
    const double noisePercent = m_settings.value(QLatin1String(kNoise)).toInt();

    if (key == QLatin1String("all") || key == QLatin1String("growing")) {
        const int count =
            key == QLatin1String("all") ? int(kPalette.size()) : growingColumnCount();
        QStringList out;
        out.reserve(count);
        for (int i = 0; i < count; ++i) {
            const double value = basicWave(kPalette[i], t, periodSec, amplitude)
                + jitter(amplitude, noisePercent) + offset;
            out << formatValue(value);
        }
        return out;
    }

    double value = 0.0;
    if (key == QLatin1String("chirp"))
        value = chirpWave(t, periodSec, amplitude);
    else if (key == QLatin1String("decay"))
        value = decayWave(t, periodSec, amplitude);
    else if (key == QLatin1String("pulse"))
        value = pulseWave(t, periodSec, amplitude);
    else if (key == QLatin1String("steps"))
        value = stepsWave(t, periodSec, amplitude);
    else if (key == QLatin1String("cosine"))
        value = basicWave(Wave::Cosine, t, periodSec, amplitude);
    else if (key == QLatin1String("square"))
        value = basicWave(Wave::Square, t, periodSec, amplitude);
    else if (key == QLatin1String("triangle"))
        value = basicWave(Wave::Triangle, t, periodSec, amplitude);
    else if (key == QLatin1String("sawtooth"))
        value = basicWave(Wave::Sawtooth, t, periodSec, amplitude);
    else if (key == QLatin1String("noise"))
        value = basicWave(Wave::Noise, t, periodSec, amplitude);
    else // "sine" и любое нераспознанное значение — рабочее умолчание, а не молчание.
        value = basicWave(Wave::Sine, t, periodSec, amplitude);

    value += jitter(amplitude, noisePercent) + offset;
    return {formatValue(value)};
}

int SignalGenChannel::growingColumnCount() const
{
    constexpr quint64 kStepSamples = 15;
    const quint64 maxColumns = quint64(kPalette.size());
    const quint64 phase = (m_counter / kStepSamples) % (2 * maxColumns);
    return int(phase < maxColumns ? phase + 1 : 2 * maxColumns - phase);
}

double SignalGenChannel::basicWave(Wave wave, double t, double periodSec, double amplitude)
{
    const double phase = 2.0 * M_PI * t / periodSec;
    switch (wave) {
    case Wave::Sine:
        return amplitude * std::sin(phase);
    case Wave::Cosine:
        return amplitude * std::cos(phase);
    case Wave::Square:
        return amplitude * (std::sin(phase) >= 0.0 ? 1.0 : -1.0);
    case Wave::Triangle:
        return amplitude * (2.0 / M_PI) * std::asin(std::sin(phase));
    case Wave::Sawtooth: {
        double cycle = t / periodSec;
        cycle -= std::floor(cycle);
        return amplitude * (2.0 * cycle - 1.0);
    }
    case Wave::Noise:
        return amplitude * (2.0 * QRandomGenerator::global()->generateDouble() - 1.0);
    }
    return 0.0;
}

double SignalGenChannel::chirpWave(double t, double periodSec, double amplitude)
{
    // Разгон вчетверо за kSweepCycles периодов, затем сброс к начальной частоте: повторяющийся
    // восходящий свип удобнее разглядывать на графике, чем один нарастающий тон, конец
    // которого дождёшься не всегда.
    constexpr double kSweepCycles = 20.0;
    const double sweepSec = periodSec * kSweepCycles;
    const double tc = std::fmod(t, sweepSec);
    const double f0 = 1.0 / periodSec;
    const double rate = 3.0 * f0 / sweepSec;
    const double phase = 2.0 * M_PI * (f0 * tc + 0.5 * rate * tc * tc);
    return amplitude * std::sin(phase);
}

double SignalGenChannel::decayWave(double t, double periodSec, double amplitude)
{
    // Затухающая синусоида, перезапускаемая каждые kBurstCycles периодов — «отклик на
    // импульс», повторённый достаточно часто, чтобы не ждать его вручную.
    constexpr double kBurstCycles = 8.0;
    const double burstSec = periodSec * kBurstCycles;
    const double tc = std::fmod(t, burstSec);
    const double tau = burstSec / 4.0;
    return amplitude * std::exp(-tc / tau) * std::sin(2.0 * M_PI * tc / periodSec);
}

double SignalGenChannel::pulseWave(double t, double periodSec, double amplitude)
{
    constexpr double kDutyCycle = 0.08;
    double cycle = t / periodSec;
    cycle -= std::floor(cycle);
    return cycle < kDutyCycle ? amplitude : 0.0;
}

double SignalGenChannel::stepsWave(double t, double periodSec, double amplitude)
{
    constexpr int kLevels = 5;
    // fmod() берётся раньше приведения к int: на многочасовой сессии с коротким периодом
    // t / periodSec выходит за пределы int, и обрезание переполнением дало бы случайный
    // уровень вместо ровной лестницы.
    const int level = int(std::fmod(std::floor(t / periodSec), double(kLevels)));
    // Центрировано вокруг нуля: пять уровней поровну от -amplitude до +amplitude.
    return amplitude * (2.0 * level / double(kLevels - 1) - 1.0);
}

double SignalGenChannel::jitter(double amplitude, double noisePercent)
{
    if (noisePercent <= 0.0)
        return 0.0;
    return amplitude * (noisePercent / 100.0)
        * (2.0 * QRandomGenerator::global()->generateDouble() - 1.0);
}

QString SignalGenChannel::formatValue(double value)
{
    return QString::number(value, 'f', 3);
}

void SignalGenChannel::setState(ChannelState state, const QString &detail)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(m_state, detail);
}

} // namespace spotty
