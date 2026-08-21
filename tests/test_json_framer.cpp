/**
 * \file test_json_framer.cpp
 * \brief Тесты выделения документов JSON из построчного потока.
 */
#include <spotty/data/JsonFramer.h>

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

using namespace spotty;

namespace {

/// \brief Миллисекунды в наносекундах — отметки в тестах задаются в миллисекундах.
constexpr qint64 ms(qint64 value)
{
    return value * 1'000'000;
}

} // namespace

TEST(JsonFramer, ParsesSingleLineObject)
{
    JsonFramer framer;
    const auto doc = framer.feed(QStringLiteral(R"({"a":1})"), 0);

    ASSERT_TRUE(doc.has_value());
    EXPECT_TRUE(doc->isObject());
    EXPECT_EQ(doc->object().value(QStringLiteral("a")).toInt(), 1);
    EXPECT_EQ(framer.counters().documents, 1u);
}

TEST(JsonFramer, ParsesSingleLineArray)
{
    JsonFramer framer;
    const auto doc = framer.feed(QStringLiteral("[1,2]"), 0);

    ASSERT_TRUE(doc.has_value());
    EXPECT_TRUE(doc->isArray());
    EXPECT_EQ(doc->array().size(), 2);
}

TEST(JsonFramer, SkipsPlainTextWithoutStartingAccumulation)
{
    JsonFramer framer;
    EXPECT_FALSE(framer.feed(QStringLiteral("boot ok"), 0).has_value());

    EXPECT_FALSE(framer.isPending());
    EXPECT_EQ(framer.counters().textLines, 1u);
}

TEST(JsonFramer, IgnoresBareScalars)
{
    // «42» — валидный JSON по RFC, но устройства печатают числа в логах постоянно, и
    // каждое такое число становилось бы документом.
    JsonFramer framer;
    EXPECT_FALSE(framer.feed(QStringLiteral("42"), 0).has_value());
    EXPECT_FALSE(framer.feed(QStringLiteral("\"hello\""), 0).has_value());
    EXPECT_FALSE(framer.feed(QStringLiteral("true"), 0).has_value());

    EXPECT_EQ(framer.counters().documents, 0u);
    EXPECT_EQ(framer.counters().textLines, 3u);
}

TEST(JsonFramer, AssemblesPrettyPrintedObject)
{
    JsonFramer framer;
    EXPECT_FALSE(framer.feed(QStringLiteral("{"), 0).has_value());
    EXPECT_TRUE(framer.isPending());
    EXPECT_FALSE(framer.feed(QStringLiteral("  \"temp\": 23.5,"), ms(1)).has_value());
    EXPECT_FALSE(framer.feed(QStringLiteral("  \"ok\": true"), ms(2)).has_value());

    const auto doc = framer.feed(QStringLiteral("}"), ms(3));
    ASSERT_TRUE(doc.has_value());
    EXPECT_DOUBLE_EQ(doc->object().value(QStringLiteral("temp")).toDouble(), 23.5);
    EXPECT_FALSE(framer.isPending());
}

TEST(JsonFramer, BracesInsideStringLiteralsDoNotBreakBalance)
{
    JsonFramer framer;
    const auto doc = framer.feed(QStringLiteral(R"({"s":"}{"})"), 0);

    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(doc->object().value(QStringLiteral("s")).toString(), QStringLiteral("}{"));
}

TEST(JsonFramer, EscapedQuoteKeepsScannerInsideString)
{
    JsonFramer framer;
    EXPECT_FALSE(framer.feed(QStringLiteral("{"), 0).has_value());
    EXPECT_FALSE(framer.feed(QStringLiteral(R"(  "s": "a\"}b",)"), ms(1)).has_value());
    // Если бы экранированная кавычка закрыла литерал, «}» внутри неё уронил бы баланс и
    // документ собрался бы на строку раньше.
    EXPECT_TRUE(framer.isPending());
    EXPECT_FALSE(framer.feed(QStringLiteral("  \"n\": 1"), ms(2)).has_value());

    const auto doc = framer.feed(QStringLiteral("}"), ms(3));
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(doc->object().value(QStringLiteral("s")).toString(), QStringLiteral("a\"}b"));
}

TEST(JsonFramer, GivesUpAfterTooManyPendingLines)
{
    JsonFramer framer;
    framer.setMaxPendingLines(4);
    framer.setPendingTimeoutMs(0); // Проверяем именно предел строк.

    framer.feed(QStringLiteral("{"), 0);
    for (int i = 0; i < 10; ++i)
        framer.feed(QStringLiteral("  \"a\": 1,"), ms(i + 1));

    EXPECT_FALSE(framer.isPending());
    EXPECT_EQ(framer.counters().abandoned, 1u);

    // После сброса поток разбирается дальше как ни в чём не бывало.
    EXPECT_TRUE(framer.feed(QStringLiteral(R"({"b":2})"), ms(50)).has_value());
}

TEST(JsonFramer, GivesUpOnUnclosedBraceAfterTimeout)
{
    // Потерянная закрывающая скобка без таймаута проглотила бы весь последующий поток:
    // каждая строка «{...}» даёт +1 и −1, и баланс остаётся положительным навсегда.
    JsonFramer framer;
    framer.setPendingTimeoutMs(2000);

    framer.feed(QStringLiteral("{"), 0);
    EXPECT_TRUE(framer.isPending());

    const auto doc = framer.feed(QStringLiteral(R"({"b":2})"), ms(3000));
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(doc->object().value(QStringLiteral("b")).toInt(), 2);
    EXPECT_EQ(framer.counters().abandoned, 1u);
}

TEST(JsonFramer, BalancedButInvalidLineIsDropped)
{
    JsonFramer framer;
    EXPECT_FALSE(framer.feed(QStringLiteral(R"({"a":})"), 0).has_value());

    EXPECT_FALSE(framer.isPending());
    EXPECT_EQ(framer.counters().malformed, 1u);
}

TEST(JsonFramer, LastArrayElementDoesNotHijackAccumulation)
{
    // Регрессия: последний элемент массива приходит без запятой и сам по себе является
    // валидным документом. Правило «валидная строка отменяет накопление» сломало бы здесь
    // штатный разбор, поэтому его нет.
    JsonFramer framer;
    EXPECT_FALSE(framer.feed(QStringLiteral("["), 0).has_value());
    EXPECT_FALSE(framer.feed(QStringLiteral(R"(  {"id":1},)"), ms(1)).has_value());
    EXPECT_FALSE(framer.feed(QStringLiteral(R"(  {"id":2})"), ms(2)).has_value());

    const auto doc = framer.feed(QStringLiteral("]"), ms(3));
    ASSERT_TRUE(doc.has_value());
    ASSERT_TRUE(doc->isArray());
    EXPECT_EQ(doc->array().size(), 2);
    EXPECT_EQ(framer.counters().documents, 1u);
}

TEST(JsonFramer, ResetDropsAccumulation)
{
    JsonFramer framer;
    framer.feed(QStringLiteral("{"), 0);
    ASSERT_TRUE(framer.isPending());

    framer.reset();
    EXPECT_FALSE(framer.isPending());
    // Хвост брошенного документа теперь просто текст, а не продолжение.
    EXPECT_FALSE(framer.feed(QStringLiteral("  \"a\": 1"), ms(1)).has_value());
}

TEST(JsonFramer, ToleratesIndentationAndCarriageReturns)
{
    JsonFramer framer;
    const auto doc = framer.feed(QStringLiteral("   {\"a\":1}\r"), 0);
    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(doc->object().value(QStringLiteral("a")).toInt(), 1);
}

TEST(JsonFramer, HandlesUtf8InKeysAndValues)
{
    JsonFramer framer;
    const auto doc = framer.feed(QString::fromUtf8(R"({"датчик":"тепло"})"), 0);

    ASSERT_TRUE(doc.has_value());
    EXPECT_EQ(doc->object().value(QString::fromUtf8("датчик")).toString(),
              QString::fromUtf8("тепло"));
}

TEST(JsonFramer, EmptyLinesOutsideAccumulationAreIgnored)
{
    JsonFramer framer;
    EXPECT_FALSE(framer.feed(QString(), 0).has_value());
    EXPECT_FALSE(framer.feed(QStringLiteral("   "), ms(1)).has_value());

    EXPECT_EQ(framer.counters().textLines, 0u);
    EXPECT_FALSE(framer.isPending());
}
