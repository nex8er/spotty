/**
 * \file DataGenerator.cpp
 * \brief Реализация spotty::DataGenerator.
 */
#include "DataGenerator.h"

#include <QRandomGenerator>

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
    }

    return result;
}

void DataGenerator::reset()
{
    m_counter = 0;
    m_rampOffset = 0;
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
    case Pattern::Counter:
        break;
    }
    return tr("Packet counter");
}

} // namespace spotty
