/**
 * \file DataGenerator.cpp
 * \brief Реализация spotty::DataGenerator.
 */
#include <spotty/data/DataGenerator.h>

#include <QRandomGenerator>

#include <cmath>
#include <limits>

namespace spotty {

namespace {

/// \brief Печатные символы для Pattern::AsciiText.
constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
constexpr int kAlphabetSize = sizeof(kAlphabet) - 1;

/// \brief Привести произвольное целое к диапазону одного байта без потери знака шага.
int byteValue(int value)
{
    return (value % 256 + 256) % 256;
}

/// \brief Сложить счётчик без неопределённого поведения при переполнении знакового типа.
qint64 advanceCounter(qint64 value, qint64 increment)
{
    if (increment > 0 && value > std::numeric_limits<qint64>::max() - increment)
        return std::numeric_limits<qint64>::max();
    if (increment < 0 && value < std::numeric_limits<qint64>::min() - increment)
        return std::numeric_limits<qint64>::min();
    return value + increment;
}

} // namespace

void DataGenerator::setLength(int length)
{
    m_length = qBound(1, length, 65536);
}

void DataGenerator::setRandomRange(int minimum, int maximum)
{
    m_randomMinimum = qBound(0, qMin(minimum, maximum), 255);
    m_randomMaximum = qBound(0, qMax(minimum, maximum), 255);
}

QByteArray DataGenerator::generate()
{
    QByteArray result = build(m_counter, m_packetIndex, m_rampOffset);

    switch (m_pattern) {
    case Pattern::Counter:
        m_counter = advanceCounter(m_counter, m_counterIncrement);
        break;
    case Pattern::Ramp:
        m_rampOffset = byteValue(m_rampOffset + m_length * m_rampIncrement);
        break;
    case Pattern::AsciiText:
    case Pattern::Sine:
    case Pattern::Square:
    case Pattern::Triangle:
    case Pattern::Sawtooth:
        ++m_packetIndex;
        break;
    case Pattern::Random:
    case Pattern::Fixed:
        break;
    }

    return result;
}

QByteArray DataGenerator::preview() const
{
    return build(m_counter, m_packetIndex, m_rampOffset);
}

QByteArray DataGenerator::build(qint64 counter, quint64 packetIndex, int rampOffset) const
{
    QByteArray result;
    result.reserve(m_prefix.size() + m_length);

    switch (m_pattern) {
    case Pattern::Counter: {
        // Номер посылки, дополненный нулями до нужной длины: по нему сразу видно, какая
        // посылка не дошла.
        if (counter < 0 && m_length > 1) {
            // Знак занимает один байт, а разряды дополняются отдельно: "-007" понятнее
            // и стабильнее, чем "00-7" при фиксированной ширине пакета.
            const qulonglong magnitude = qulonglong(-(counter + 1)) + 1;
            const QByteArray digits = QByteArray::number(magnitude);
            const int digitLength = m_length - 1;
            result = QByteArrayLiteral("-")
                     + (digits.size() >= digitLength
                            ? digits.right(digitLength)
                            : QByteArray(digitLength - digits.size(), '0') + digits);
        } else {
            const QByteArray number = QByteArray::number(counter);
            result = number.size() >= m_length
                         ? number.right(m_length)
                         : QByteArray(m_length - number.size(), '0') + number;
        }
        break;
    }

    case Pattern::Random: {
        result.resize(m_length);
        const int rangeSize = m_randomMaximum - m_randomMinimum + 1;
        for (int i = 0; i < result.size(); ++i)
            result[i] = char(m_randomMinimum + QRandomGenerator::global()->bounded(rangeSize));
        break;
    }

    case Pattern::Fixed:
        result = QByteArray(m_length, char(m_fixedByte));
        break;

    case Pattern::Ramp:
        // Пила продолжается между посылками, поэтому разрыв в потоке виден по скачку
        // значения, а не только по пропаже посылки целиком.
        for (int i = 0; i < m_length; ++i)
            result.append(char(byteValue(rampOffset + i * m_rampIncrement)));
        break;

    case Pattern::AsciiText:
        for (int i = 0; i < m_length; ++i)
            result.append(kAlphabet[(int(packetIndex) + i) % kAlphabetSize]);
        break;

    case Pattern::Sine:
    case Pattern::Square:
    case Pattern::Triangle:
    case Pattern::Sawtooth: {
        // Фаза от 0 до 1 внутри периода. Считается по номеру посылки, а не по времени:
        // период отправки операционная система не выдерживает точно, и форма, привязанная
        // к часам, плыла бы от каждого опоздания таймера.
        const double phase =
            double(packetIndex % quint64(m_wavePeriod)) / double(m_wavePeriod);

        double unit = 0.0; // 0…1
        switch (m_pattern) {
        case Pattern::Sine:
            unit = (std::sin(phase * 2.0 * M_PI) + 1.0) / 2.0;
            break;
        case Pattern::Square:
            unit = phase < m_dutyCycle / 100.0 ? 1.0 : 0.0;
            break;
        case Pattern::Triangle:
            unit = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
            break;
        case Pattern::Sawtooth:
            unit = phase;
            break;
        default:
            break;
        }

        result = QByteArray::number(m_offset + unit * m_amplitude, 'f', m_wavePrecision);
        break;
    }
    }

    return m_prefix + result;
}

void DataGenerator::reset()
{
    m_counter = m_counterStart;
    m_packetIndex = 0;
    m_rampOffset = m_rampStart;
}

bool DataGenerator::isWaveform(Pattern pattern)
{
    switch (pattern) {
    case Pattern::Sine:
    case Pattern::Square:
    case Pattern::Triangle:
    case Pattern::Sawtooth:
        return true;
    default:
        return false;
    }
}

void DataGenerator::setWavePeriod(int samples)
{
    m_wavePeriod = qMax(1, samples);
}

void DataGenerator::setRampStart(int value)
{
    m_rampStart = byteValue(value);
}

void DataGenerator::setRampIncrement(int increment)
{
    m_rampIncrement = qBound(-255, increment, 255);
}

void DataGenerator::setAmplitude(double amplitude)
{
    m_amplitude = qMax(0.0, std::isfinite(amplitude) ? amplitude : 0.0);
}

void DataGenerator::setOffset(double offset)
{
    m_offset = std::isfinite(offset) ? offset : 0.0;
}

void DataGenerator::setDutyCycle(double percent)
{
    m_dutyCycle = qBound(1.0, std::isfinite(percent) ? percent : 50.0, 99.0);
}

void DataGenerator::setWavePrecision(int digits)
{
    m_wavePrecision = qBound(0, digits, 12);
}

QString DataGenerator::patternName(Pattern pattern)
{
    switch (pattern) {
    case Pattern::Random:
        return tr("Random values");
    case Pattern::Fixed:
        return tr("Fixed value");
    case Pattern::Ramp:
        return tr("Ramp 00..FF");
    case Pattern::AsciiText:
        return tr("ASCII text");
    case Pattern::Sine:
        return tr("Sine");
    case Pattern::Square:
        return tr("Square wave");
    case Pattern::Triangle:
        return tr("Triangle wave");
    case Pattern::Sawtooth:
        return tr("Sawtooth wave");
    case Pattern::Counter:
        break;
    }
    return tr("Packet counter");
}

} // namespace spotty
