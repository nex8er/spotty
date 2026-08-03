/**
 * \file test_packetizer.cpp
 * \brief Тесты spotty::Packetizer.
 */
#include <terminal/Packetizer.h>

#include <gtest/gtest.h>

using namespace spotty;

namespace {

/// \brief Миллисекунды в наносекундах — метки времени задаются в них.
constexpr qint64 ms(qint64 value)
{
    return value * 1'000'000;
}

} // namespace

TEST(Packetizer, StreamModePassesEverythingThrough)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::Stream);

    const auto packets = packetizer.feed(QByteArrayLiteral("anything"), 0);

    ASSERT_EQ(packets.size(), 1);
    EXPECT_EQ(packets[0].data, QByteArrayLiteral("anything"));
    // Границу в этом режиме определяет разборщик по переводам строк, а не пакетизатор.
    EXPECT_FALSE(packets[0].terminatesLine);
}

TEST(Packetizer, FixedLengthSplitsExactly)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::FixedLength);
    packetizer.setFixedLength(4);

    const auto packets = packetizer.feed(QByteArrayLiteral("0123456789"), 0);

    ASSERT_EQ(packets.size(), 2);
    EXPECT_EQ(packets[0].data, QByteArrayLiteral("0123"));
    EXPECT_EQ(packets[1].data, QByteArrayLiteral("4567"));
    EXPECT_TRUE(packets[0].terminatesLine);

    // Остаток ждёт продолжения, а не выдаётся куском.
    EXPECT_TRUE(packetizer.hasPending());
    EXPECT_EQ(packetizer.flush().data, QByteArrayLiteral("89"));
}

TEST(Packetizer, FixedLengthAcrossChunks)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::FixedLength);
    packetizer.setFixedLength(4);

    EXPECT_TRUE(packetizer.feed(QByteArrayLiteral("01"), 0).isEmpty());

    const auto packets = packetizer.feed(QByteArrayLiteral("23"), 0);
    ASSERT_EQ(packets.size(), 1);
    EXPECT_EQ(packets[0].data, QByteArrayLiteral("0123"));
}

TEST(Packetizer, DelimiterKeepsSeparatorInPacket)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::Delimiter);
    packetizer.setDelimiter(QByteArrayLiteral("\n"));

    const auto packets = packetizer.feed(QByteArrayLiteral("one\n"), 0);

    ASSERT_EQ(packets.size(), 1);
    // Для монитора порта важно видеть всё, что реально пришло, а не очищенную версию.
    EXPECT_EQ(packets[0].data, QByteArrayLiteral("one\n"));
}

TEST(Packetizer, MultipleDelimitersInOneChunk)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::Delimiter);
    packetizer.setDelimiter(QByteArrayLiteral("\n"));

    // Тут была ошибка: каждый следующий пакет отсчитывался от начала накопителя и включал
    // в себя все предыдущие.
    const auto packets = packetizer.feed(QByteArrayLiteral("one\ntwo\nthree\n"), 0);

    ASSERT_EQ(packets.size(), 3);
    EXPECT_EQ(packets[0].data, QByteArrayLiteral("one\n"));
    EXPECT_EQ(packets[1].data, QByteArrayLiteral("two\n"));
    EXPECT_EQ(packets[2].data, QByteArrayLiteral("three\n"));
    EXPECT_FALSE(packetizer.hasPending());
}

TEST(Packetizer, MultiByteDelimiter)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::Delimiter);
    packetizer.setDelimiter(QByteArrayLiteral("\r\n"));

    const auto packets = packetizer.feed(QByteArrayLiteral("a\r\nb\r\n"), 0);

    ASSERT_EQ(packets.size(), 2);
    EXPECT_EQ(packets[0].data, QByteArrayLiteral("a\r\n"));
    EXPECT_EQ(packets[1].data, QByteArrayLiteral("b\r\n"));
}

TEST(Packetizer, DelimiterSplitAcrossChunks)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::Delimiter);
    packetizer.setDelimiter(QByteArrayLiteral("\r\n"));

    // Разделитель может прийти разорванным между чтениями.
    EXPECT_TRUE(packetizer.feed(QByteArrayLiteral("data\r"), 0).isEmpty());

    const auto packets = packetizer.feed(QByteArrayLiteral("\nmore"), 0);
    ASSERT_EQ(packets.size(), 1);
    EXPECT_EQ(packets[0].data, QByteArrayLiteral("data\r\n"));
}

TEST(Packetizer, EmptyDelimiterPassesThrough)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::Delimiter);
    packetizer.setDelimiter({});

    const auto packets = packetizer.feed(QByteArrayLiteral("abc"), 0);

    // Пустой разделитель не должен приводить к бесконечному циклу поиска.
    ASSERT_EQ(packets.size(), 1);
    EXPECT_EQ(packets[0].data, QByteArrayLiteral("abc"));
}

TEST(Packetizer, TimeoutClosesPacketOnGap)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::InterByteTimeout);
    packetizer.setTimeoutMs(20);

    EXPECT_TRUE(packetizer.feed(QByteArrayLiteral("first"), ms(0)).isEmpty());

    // Данные пришли через 50 мс — пауза больше порога, значит предыдущее сообщение
    // закончилось.
    const auto packets = packetizer.feed(QByteArrayLiteral("second"), ms(50));

    ASSERT_EQ(packets.size(), 1);
    EXPECT_EQ(packets[0].data, QByteArrayLiteral("first"));
    EXPECT_TRUE(packets[0].terminatesLine);

    EXPECT_EQ(packetizer.flush().data, QByteArrayLiteral("second"));
}

TEST(Packetizer, TimeoutAccumulatesWithinGap)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::InterByteTimeout);
    packetizer.setTimeoutMs(20);

    EXPECT_TRUE(packetizer.feed(QByteArrayLiteral("ab"), ms(0)).isEmpty());
    EXPECT_TRUE(packetizer.feed(QByteArrayLiteral("cd"), ms(5)).isEmpty());
    EXPECT_TRUE(packetizer.feed(QByteArrayLiteral("ef"), ms(10)).isEmpty());

    EXPECT_EQ(packetizer.flush().data, QByteArrayLiteral("abcdef"));
}

TEST(Packetizer, FlushOnEmptyReturnsNothing)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::InterByteTimeout);

    EXPECT_TRUE(packetizer.flush().data.isEmpty());
    EXPECT_FALSE(packetizer.hasPending());
}

TEST(Packetizer, ChangingModeDropsPending)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::FixedLength);
    packetizer.setFixedLength(8);
    packetizer.feed(QByteArrayLiteral("abc"), 0);
    ASSERT_TRUE(packetizer.hasPending());

    // Остаток принадлежал прежнему правилу и по новому смысла не имеет.
    packetizer.setMode(Packetizer::Mode::Delimiter);

    EXPECT_FALSE(packetizer.hasPending());
}

TEST(Packetizer, ResetDropsPending)
{
    Packetizer packetizer;
    packetizer.setMode(Packetizer::Mode::FixedLength);
    packetizer.setFixedLength(8);
    packetizer.feed(QByteArrayLiteral("abc"), 0);

    packetizer.reset();

    EXPECT_FALSE(packetizer.hasPending());
}

TEST(Packetizer, EmptyInputProducesNothing)
{
    Packetizer packetizer;
    EXPECT_TRUE(packetizer.feed({}, 0).isEmpty());
}
