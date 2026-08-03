/**
 * \file test_data_codec.cpp
 * \brief Тесты spotty::DataCodec.
 */
#include <terminal/DataCodec.h>

#include <gtest/gtest.h>

using namespace spotty;

TEST(DataCodec, TerminationBytes)
{
    EXPECT_EQ(DataCodec::terminationBytes(DataCodec::Termination::None), QByteArray());
    EXPECT_EQ(DataCodec::terminationBytes(DataCodec::Termination::Lf),
              QByteArrayLiteral("\n"));
    EXPECT_EQ(DataCodec::terminationBytes(DataCodec::Termination::Cr),
              QByteArrayLiteral("\r"));
    EXPECT_EQ(DataCodec::terminationBytes(DataCodec::Termination::CrLf),
              QByteArrayLiteral("\r\n"));
    EXPECT_EQ(DataCodec::terminationBytes(DataCodec::Termination::Nul), QByteArray(1, '\0'));
}

TEST(DataCodec, TextWithTermination)
{
    QString error;
    const QByteArray data = DataCodec::encode(QStringLiteral("AT"), DataCodec::Format::Text,
                                              DataCodec::Termination::CrLf, &error);

    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(data, QByteArrayLiteral("AT\r\n"));
}

TEST(DataCodec, EmptyInputStillSendsTermination)
{
    QString error;
    const QByteArray data = DataCodec::encode({}, DataCodec::Format::Text,
                                              DataCodec::Termination::Lf, &error);

    // Отправка одной терминации — это «нажать Enter», совершенно осмысленное действие.
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(data, QByteArrayLiteral("\n"));
}

TEST(DataCodec, HexAcceptsCommonSeparators)
{
    // Байты копируют откуда угодно: из даташита через пробел, из лога анализатора через
    // двоеточие, из чужого кода через «0x…, 0x…».
    const QStringList variants = {
        QStringLiteral("01A5FF"),
        QStringLiteral("01 A5 FF"),
        QStringLiteral("01:A5:FF"),
        QStringLiteral("01-A5-FF"),
        QStringLiteral("0x01, 0xA5, 0xFF"),
        QStringLiteral("01  a5\tff"),
    };

    for (const QString &variant : variants) {
        QString error;
        const QByteArray data = DataCodec::fromHex(variant, &error);
        EXPECT_TRUE(error.isEmpty()) << variant.toStdString();
        EXPECT_EQ(data, QByteArray::fromHex("01A5FF")) << variant.toStdString();
    }
}

TEST(DataCodec, HexRejectsOddDigitCount)
{
    QString error;
    const QByteArray data = DataCodec::fromHex(QStringLiteral("ABC"), &error);

    EXPECT_TRUE(data.isEmpty());
    EXPECT_FALSE(error.isEmpty());
}

TEST(DataCodec, HexRejectsNonHexCharacter)
{
    QString error;
    const QByteArray data = DataCodec::fromHex(QStringLiteral("01 ZZ"), &error);

    EXPECT_TRUE(data.isEmpty());
    EXPECT_FALSE(error.isEmpty());
}

TEST(DataCodec, HexEmptyIsNotAnError)
{
    QString error;
    const QByteArray data = DataCodec::fromHex(QStringLiteral("   "), &error);

    EXPECT_TRUE(data.isEmpty());
    EXPECT_TRUE(error.isEmpty());
}

TEST(DataCodec, HexRoundTrip)
{
    const QByteArray original = QByteArray::fromHex("DEADBEEF00FF");
    const QString text = DataCodec::toHex(original);

    QString error;
    EXPECT_EQ(DataCodec::fromHex(text, &error), original);
    EXPECT_TRUE(error.isEmpty());
}

TEST(DataCodec, Base64Valid)
{
    QString error;
    const QByteArray data = DataCodec::encode(QStringLiteral("aGVsbG8="),
                                              DataCodec::Format::Base64,
                                              DataCodec::Termination::None, &error);

    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(data, QByteArrayLiteral("hello"));
}

TEST(DataCodec, Base64RejectsGarbage)
{
    QString error;
    const QByteArray data = DataCodec::encode(QStringLiteral("not base64!!!"),
                                              DataCodec::Format::Base64,
                                              DataCodec::Termination::None, &error);

    // Разбор строгий намеренно: тихо проглоченный мусор ушёл бы в порт, и найти причину
    // было бы нечем.
    EXPECT_TRUE(data.isEmpty());
    EXPECT_FALSE(error.isEmpty());
}

TEST(DataCodec, HexErrorPreventsSending)
{
    QString error;
    const QByteArray data = DataCodec::encode(QStringLiteral("XY"), DataCodec::Format::Hex,
                                              DataCodec::Termination::CrLf, &error);

    // Ошибка кодирования не должна приводить к отправке одной лишь терминации.
    EXPECT_TRUE(data.isEmpty());
    EXPECT_FALSE(error.isEmpty());
}

TEST(DataCodec, ToHexIsUpperCaseSpaced)
{
    EXPECT_EQ(DataCodec::toHex(QByteArray::fromHex("0aff")), QStringLiteral("0A FF"));
}

TEST(DataCodec, Utf8TextIsEncoded)
{
    QString error;
    const QByteArray data = DataCodec::encode(QStringLiteral("Привет"),
                                              DataCodec::Format::Text,
                                              DataCodec::Termination::None, &error);

    EXPECT_EQ(data, QStringLiteral("Привет").toUtf8());
}
