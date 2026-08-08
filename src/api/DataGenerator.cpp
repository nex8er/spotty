/**
 * \file DataGenerator.cpp
 * \brief Реализация spotty::DataGenerator.
 */
#include <spotty/data/DataGenerator.h>

#include <QRandomGenerator>

#include <cmath>

namespace spotty {

namespace {

/// \brief Печатные символы для Pattern::AsciiText.
constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
constexpr int kAlphabetSize = sizeof(kAlphabet) - 1;

} // namespace

void DataGenerator::setLength(int length)
{
    m_length = qBound(1, length, 65536);
}

QByteArray DataGenerator::generate()
{
    QByteArray result = build(m_counter, m_rampOffset);

    ++m_counter;
    m_rampOffset = (m_rampOffset + m_length) % 256;

    return result;
}

QByteArray DataGenerator::preview() const
{
    return build(m_counter, m_rampOffset);
}

QByteArray DataGenerator::build(quint64 counter, int rampOffset) const
{
    QByteArray result;
    result.reserve(m_length);

    switch (m_pattern) {
    case Pattern::Counter: {
        // Номер посылки, дополненный нулями до нужной длины: по нему сразу видно, какая
        // посылка не дошла.
        const QByteArray number = QByteArray::number(qulonglong(counter));
        if (number.size() >= m_length) {
            result = number.right(m_length);
        } else {
            result = QByteArray(m_length - number.size(), '0') + number;
        }
        break;
    }

    case Pattern::Random: {
        result.resize(m_length);
        QRandomGenerator::global()->generate(result.begin(), result.end());
        break;
    }

    case Pattern::Fixed:
        result = QByteArray(m_length, char(m_fixedByte));
        break;

    case Pattern::Ramp:
        // Пила продолжается между посылками, поэтому разрыв в потоке виден по скачку
        // значения, а не только по пропаже посылки целиком.
        for (int i = 0; i < m_length; ++i)
            result.append(char((rampOffset + i) % 256));
        break;

    case Pattern::AsciiText:
        for (int i = 0; i < m_length; ++i)
            result.append(kAlphabet[(int(counter) + i) % kAlphabetSize]);
        break;

    case Pattern::Sine:
    case Pattern::Square:
    case Pattern::Triangle:
    case Pattern::Sawtooth: {
        // Фаза от 0 до 1 внутри периода. Считается по номеру посылки, а не по времени:
        // период отправки операционная система не выдерживает точно, и форма, привязанная
        // к часам, плыла бы от каждого опоздания таймера.
        const double phase = double(counter % quint64(m_wavePeriod)) / double(m_wavePeriod);

        double unit = 0.0; // 0…1
        switch (m_pattern) {
        case Pattern::Sine:
            unit = (std::sin(phase * 2.0 * M_PI) + 1.0) / 2.0;
            break;
        case Pattern::Square:
            unit = phase < 0.5 ? 0.0 : 1.0;
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

        // Три знака после запятой: меньше — и синус превращается в лестницу на графике,
        // больше — и строка вырастает без всякой пользы для глаза.
        result = QByteArray::number(unit * m_amplitude, 'f', 3);
        break;
    }
    }

    return result;
}

void DataGenerator::reset()
{
    m_counter = 0;
    m_rampOffset = 0;
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
    // Меньше двух посылок на период — это уже не форма, а чередование двух чисел.
    m_wavePeriod = qMax(2, samples);
}

void DataGenerator::setAmplitude(double amplitude)
{
    m_amplitude = amplitude;
}

QString DataGenerator::patternName(Pattern pattern)
{
    switch (pattern) {
    case Pattern::Random:
        return tr("Random bytes");
    case Pattern::Fixed:
        return tr("Fixed byte");
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
