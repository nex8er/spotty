/**
 * \file test_csv_series.cpp
 * \brief Тесты разбора CSV в плагине графика.
 */
#include "CsvSeries.h"

#include <gtest/gtest.h>

using namespace spotty;

TEST(CsvSeries, ParsesNumericLineIntoSeries)
{
    CsvSeries series;
    EXPECT_TRUE(series.feed(QStringLiteral("1,2,3")));

    ASSERT_EQ(series.seriesCount(), 3);
    EXPECT_DOUBLE_EQ(series.values(0).first(), 1.0);
    EXPECT_DOUBLE_EQ(series.values(2).first(), 3.0);
}

TEST(CsvSeries, IgnoresNonNumericLines)
{
    CsvSeries series;
    // Устройства шлют сообщения о состоянии вперемешку с данными; считать их ошибкой
    // значило бы засорять вывод жалобами на нормальную работу.
    EXPECT_FALSE(series.feed(QStringLiteral("boot ok")));
    EXPECT_FALSE(series.feed(QStringLiteral("1,oops,3")));
    EXPECT_EQ(series.seriesCount(), 0);
}

TEST(CsvSeries, GrowsToTheWidestLine)
{
    CsvSeries series;
    series.feed(QStringLiteral("1"));
    series.feed(QStringLiteral("1,2,3"));

    EXPECT_EQ(series.seriesCount(), 3);
}

TEST(CsvSeries, DropsOldestBeyondCapacity)
{
    CsvSeries series;
    series.setCapacity(3);
    for (int i = 0; i < 10; ++i)
        series.feed(QString::number(i));

    ASSERT_EQ(series.seriesCount(), 1);
    ASSERT_EQ(series.values(0).size(), 3);
    // Показывается хвост потока: неограниченный рост съел бы память за часы работы.
    EXPECT_DOUBLE_EQ(series.values(0).first(), 7.0);
    EXPECT_DOUBLE_EQ(series.values(0).last(), 9.0);
}

TEST(CsvSeries, ShrinkingCapacityTrimsImmediately)
{
    CsvSeries series;
    for (int i = 0; i < 10; ++i)
        series.feed(QString::number(i));
    series.setCapacity(2);

    EXPECT_EQ(series.values(0).size(), 2);
}

TEST(CsvSeries, RangeCoversAllSeries)
{
    CsvSeries series;
    series.feed(QStringLiteral("1,-5"));
    series.feed(QStringLiteral("9,0"));

    double lo = 0.0;
    double hi = 0.0;
    series.range(&lo, &hi);
    EXPECT_DOUBLE_EQ(lo, -5.0);
    EXPECT_DOUBLE_EQ(hi, 9.0);
}

TEST(CsvSeries, ConstantSignalGetsANonZeroRange)
{
    CsvSeries series;
    series.feed(QStringLiteral("4"));
    series.feed(QStringLiteral("4"));

    double lo = 0.0;
    double hi = 0.0;
    series.range(&lo, &hi);
    // Без запаса линия легла бы точно на край рамки и стала невидимой.
    EXPECT_LT(lo, hi);
}

TEST(CsvSeries, EmptyRangeIsUsable)
{
    CsvSeries series;

    double lo = 42.0;
    double hi = 42.0;
    series.range(&lo, &hi);
    EXPECT_LT(lo, hi);
}

TEST(CsvSeries, SeparatorIsConfigurable)
{
    CsvSeries series;
    series.setSeparator(u';');
    EXPECT_TRUE(series.feed(QStringLiteral("1;2")));
    EXPECT_EQ(series.seriesCount(), 2);
}

TEST(CsvSeries, ClearDropsEverything)
{
    CsvSeries series;
    series.feed(QStringLiteral("1,2"));
    series.clear();
    EXPECT_EQ(series.seriesCount(), 0);
}
