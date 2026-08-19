/**
 * \file test_sample_parser.cpp
 * \brief Тесты разбора строки устройства в отсчёт.
 */
#include <spotty/data/SampleParser.h>

#include <gtest/gtest.h>

#include <QLocale>

#include <cmath>

using namespace spotty;

namespace {

SampleParser::Result parse(const SampleParser &parser, const QString &line,
                           int columns, QList<double> *values, QStringList *names)
{
    return parser.parse(line, columns, values, names);
}

} // namespace

TEST(SampleParser, EmptyFieldBecomesNaNNotAShift)
{
    // Заглавный тест: прежде «1,,3» давало два поля, и тройка попадала в колонку 1 вместо
    // колонки 2 — одно незаполненное значение молча перемешивало все последующие.
    SampleParser parser;
    QList<double> values;
    QStringList names;

    const auto result = parse(parser, QStringLiteral("1,,3"), 0, &values, &names);

    ASSERT_EQ(result.outcome, SampleParser::Outcome::Data);
    ASSERT_EQ(values.size(), 3);
    EXPECT_DOUBLE_EQ(values.at(0), 1.0);
    EXPECT_TRUE(std::isnan(values.at(1)));
    EXPECT_DOUBLE_EQ(values.at(2), 3.0);
}

TEST(SampleParser, LeadingSeparatorKeepsColumnAlignment)
{
    SampleParser parser;
    QList<double> values;
    QStringList names;

    const auto result = parse(parser, QStringLiteral(",1,2"), 0, &values, &names);

    ASSERT_EQ(result.outcome, SampleParser::Outcome::Data);
    ASSERT_EQ(values.size(), 3);
    EXPECT_TRUE(std::isnan(values.at(0)));
    EXPECT_DOUBLE_EQ(values.at(1), 1.0);
}

TEST(SampleParser, TrailingSeparatorYieldsTrailingGap)
{
    SampleParser parser;
    QList<double> values;
    QStringList names;

    const auto result = parse(parser, QStringLiteral("1,2,"), 0, &values, &names);

    ASSERT_EQ(result.outcome, SampleParser::Outcome::Data);
    ASSERT_EQ(values.size(), 3);
    EXPECT_TRUE(std::isnan(values.at(2)));
}

TEST(SampleParser, SpaceSeparatorCollapsesRuns)
{
    // Устройства выравнивают колонки пробелами. Считать каждый пробел разделителем значило
    // бы получать разное число полей на каждой строке — вторая половина прежнего бага.
    SampleParser parser;
    parser.setSeparator(u' ');
    QList<double> values;
    QStringList names;

    const auto result = parse(parser, QStringLiteral("1   23    4"), 0, &values, &names);

    ASSERT_EQ(result.outcome, SampleParser::Outcome::Data);
    ASSERT_EQ(values.size(), 3);
    EXPECT_DOUBLE_EQ(values.at(1), 23.0);
}

TEST(SampleParser, CommaSeparatorDoesNotCollapse)
{
    // Обратная сторона того же правила: запятые серией не схлопываются, иначе пропуск
    // снова начал бы сдвигать колонки.
    SampleParser parser;
    QList<double> values;
    QStringList names;

    parse(parser, QStringLiteral("1,,,4"), 0, &values, &names);
    ASSERT_EQ(values.size(), 4);
    EXPECT_DOUBLE_EQ(values.at(3), 4.0);
}

TEST(SampleParser, SingleFieldLineIsData)
{
    // Плоттер намеренно мягче CsvDetector, который требует не меньше двух полей: тот решает,
    // прятать ли строку от человека, а этот рисует график по просьбе пользователя.
    SampleParser parser;
    QList<double> values;
    QStringList names;

    EXPECT_EQ(parse(parser, QStringLiteral("42"), 0, &values, &names).outcome,
              SampleParser::Outcome::Data);
}

TEST(SampleParser, StatusMessagesAreIgnored)
{
    SampleParser parser;
    QList<double> values;
    QStringList names;

    EXPECT_EQ(parse(parser, QStringLiteral("boot ok"), 0, &values, &names).outcome,
              SampleParser::Outcome::Ignored);
}

TEST(SampleParser, MixedLineIsIgnoredNotAHeader)
{
    // «temp,25.3» — подписанное значение для человека. График по нему дал бы колонку из
    // одних пропусков.
    SampleParser parser;
    QList<double> values;
    QStringList names;

    EXPECT_EQ(parse(parser, QStringLiteral("temp,25.3"), 0, &values, &names).outcome,
              SampleParser::Outcome::Ignored);
}

TEST(SampleParser, HeaderRowIsRecognized)
{
    SampleParser parser;
    QList<double> values;
    QStringList names;

    const auto result = parse(parser, QStringLiteral("t,volt,curr"), 0, &values, &names);

    ASSERT_EQ(result.outcome, SampleParser::Outcome::Header);
    ASSERT_EQ(names.size(), 3);
    EXPECT_EQ(names.at(1), QStringLiteral("volt"));
}

TEST(SampleParser, HeaderIsRejectedWhenWidthDiffers)
{
    // Иначе случайная фраза из двух слов переименовала бы ряды шестиколоночного потока.
    SampleParser parser;
    QList<double> values;
    QStringList names;

    EXPECT_EQ(parse(parser, QStringLiteral("hello,world"), 6, &values, &names).outcome,
              SampleParser::Outcome::Ignored);
}

TEST(SampleParser, HeaderNeedsAtLeastTwoFields)
{
    SampleParser parser;
    QList<double> values;
    QStringList names;

    EXPECT_EQ(parse(parser, QStringLiteral("ready"), 0, &values, &names).outcome,
              SampleParser::Outcome::Ignored);
}

TEST(SampleParser, HeaderWithEmptyFieldIsNotAHeader)
{
    SampleParser parser;
    QList<double> values;
    QStringList names;

    EXPECT_EQ(parse(parser, QStringLiteral("t,,curr"), 0, &values, &names).outcome,
              SampleParser::Outcome::Ignored);
}

TEST(SampleParser, NanAndInfLiteralsBecomeMissing)
{
    // Пропустив бесконечность дальше, мы получили бы шкалу от минус бесконечности до плюс.
    SampleParser parser;
    QList<double> values;
    QStringList names;

    const auto result = parse(parser, QStringLiteral("1,nan,inf"), 0, &values, &names);

    ASSERT_EQ(result.outcome, SampleParser::Outcome::Data);
    ASSERT_EQ(values.size(), 3);
    EXPECT_TRUE(std::isnan(values.at(1)));
    EXPECT_TRUE(std::isnan(values.at(2)));
}

TEST(SampleParser, AllEmptyLineIsIgnored)
{
    SampleParser parser;
    QList<double> values;
    QStringList names;

    EXPECT_EQ(parse(parser, QStringLiteral(",,"), 0, &values, &names).outcome,
              SampleParser::Outcome::Ignored);
}

TEST(SampleParser, LocaleIndependentDecimalPoint)
{
    // Страховка от «починки» toDouble в QLocale::toDouble: на немецкой системе «1.5»
    // перестало бы разбираться, а «1,5» разобралось бы как одно поле вместо двух.
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));

    SampleParser parser;
    QList<double> values;
    QStringList names;

    const auto result = parse(parser, QStringLiteral("1.5,2.5"), 0, &values, &names);

    ASSERT_EQ(result.outcome, SampleParser::Outcome::Data);
    ASSERT_EQ(values.size(), 2);
    EXPECT_DOUBLE_EQ(values.at(0), 1.5);

    QLocale::setDefault(previous);
}
