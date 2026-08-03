/**
 * \file test_data_generator.cpp
 * \brief Тесты spotty::DataGenerator.
 */
#include <DataGenerator.h>

#include <gtest/gtest.h>

#include <set>

using namespace spotty;

TEST(DataGenerator, LengthIsRespected)
{
    DataGenerator generator;

    for (const int length : {1, 7, 16, 1024}) {
        generator.setLength(length);
        for (const auto pattern : {DataGenerator::Pattern::Counter,
                                   DataGenerator::Pattern::Random,
                                   DataGenerator::Pattern::Fixed,
                                   DataGenerator::Pattern::Ramp,
                                   DataGenerator::Pattern::AsciiText}) {
            generator.setPattern(pattern);
            EXPECT_EQ(generator.generate().size(), length)
                << "length " << length << ", pattern " << int(pattern);
        }
    }
}

TEST(DataGenerator, LengthIsClamped)
{
    DataGenerator generator;

    generator.setLength(0);
    EXPECT_EQ(generator.length(), 1);

    generator.setLength(-5);
    EXPECT_EQ(generator.length(), 1);

    generator.setLength(1'000'000);
    EXPECT_EQ(generator.length(), 65536);
}

TEST(DataGenerator, FixedPatternRepeatsTheByte)
{
    DataGenerator generator;
    generator.setPattern(DataGenerator::Pattern::Fixed);
    generator.setFixedByte(0xA5);
    generator.setLength(4);

    EXPECT_EQ(generator.generate(), QByteArray(4, char(0xA5)));
}

TEST(DataGenerator, CounterIsZeroPaddedAndIncrements)
{
    DataGenerator generator;
    generator.setPattern(DataGenerator::Pattern::Counter);
    generator.setLength(6);

    EXPECT_EQ(generator.generate(), QByteArrayLiteral("000000"));
    EXPECT_EQ(generator.generate(), QByteArrayLiteral("000001"));
    EXPECT_EQ(generator.generate(), QByteArrayLiteral("000002"));
}

TEST(DataGenerator, RampContinuesAcrossPackets)
{
    DataGenerator generator;
    generator.setPattern(DataGenerator::Pattern::Ramp);
    generator.setLength(4);

    EXPECT_EQ(generator.generate(), QByteArray::fromHex("00010203"));
    // Пила продолжается: разрыв в потоке виден по скачку значения, а не только по
    // пропаже посылки целиком.
    EXPECT_EQ(generator.generate(), QByteArray::fromHex("04050607"));
}

TEST(DataGenerator, RampWrapsAtByteBoundary)
{
    DataGenerator generator;
    generator.setPattern(DataGenerator::Pattern::Ramp);
    generator.setLength(256);

    const QByteArray first = generator.generate();
    ASSERT_EQ(first.size(), 256);
    EXPECT_EQ(quint8(first.at(0)), 0);
    EXPECT_EQ(quint8(first.at(255)), 255);

    // После полного круга отсчёт начинается заново.
    EXPECT_EQ(quint8(generator.generate().at(0)), 0);
}

TEST(DataGenerator, PreviewDoesNotAdvanceState)
{
    DataGenerator generator;
    generator.setPattern(DataGenerator::Pattern::Counter);
    generator.setLength(4);

    // Предпросмотр показывает то, что уйдёт в порт, но счётчик сдвигать не должен.
    EXPECT_EQ(generator.preview(), QByteArrayLiteral("0000"));
    EXPECT_EQ(generator.preview(), QByteArrayLiteral("0000"));
    EXPECT_EQ(generator.generate(), QByteArrayLiteral("0000"));
    EXPECT_EQ(generator.preview(), QByteArrayLiteral("0001"));
}

TEST(DataGenerator, ResetRestartsCounterAndRamp)
{
    DataGenerator generator;
    generator.setPattern(DataGenerator::Pattern::Counter);
    generator.setLength(4);

    generator.generate();
    generator.generate();
    generator.reset();

    EXPECT_EQ(generator.generate(), QByteArrayLiteral("0000"));
}

TEST(DataGenerator, CounterLongerThanLengthKeepsLowDigits)
{
    DataGenerator generator;
    generator.setPattern(DataGenerator::Pattern::Counter);
    generator.setLength(2);

    // После ста вызовов счётчик равен 100.
    for (int i = 0; i < 100; ++i)
        generator.generate();

    // Номер 100 в два знака не помещается; берутся младшие разряды — «00», — а длина
    // посылки остаётся ровно заданной.
    const QByteArray data = generator.generate();
    EXPECT_EQ(data.size(), 2);
    EXPECT_EQ(data, QByteArrayLiteral("00"));
}

TEST(DataGenerator, AsciiTextIsPrintable)
{
    DataGenerator generator;
    generator.setPattern(DataGenerator::Pattern::AsciiText);
    generator.setLength(64);

    const QByteArray data = generator.generate();
    for (const char byte : data) {
        const auto value = quint8(byte);
        EXPECT_GE(value, 0x20) << "non-printable byte in ASCII pattern";
        EXPECT_LT(value, 0x7F) << "non-printable byte in ASCII pattern";
    }
}

TEST(DataGenerator, RandomProducesDifferentPackets)
{
    DataGenerator generator;
    generator.setPattern(DataGenerator::Pattern::Random);
    generator.setLength(32);

    std::set<QByteArray> seen;
    for (int i = 0; i < 8; ++i)
        seen.insert(generator.generate());

    // Совпадение двух случайных блоков по 32 байта практически невозможно; одинаковые
    // блоки означали бы, что генератор не переинициализируется.
    EXPECT_GT(seen.size(), 1u);
}

TEST(DataGenerator, PatternNamesAreNotEmpty)
{
    for (const auto pattern : {DataGenerator::Pattern::Counter,
                               DataGenerator::Pattern::Random,
                               DataGenerator::Pattern::Fixed,
                               DataGenerator::Pattern::Ramp,
                               DataGenerator::Pattern::AsciiText}) {
        EXPECT_FALSE(DataGenerator::patternName(pattern).isEmpty());
    }
}
