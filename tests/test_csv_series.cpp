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
    EXPECT_TRUE(series.feed(QStringLiteral("1,2,3"), 0));

    ASSERT_EQ(series.seriesCount(), 3);
    EXPECT_DOUBLE_EQ(series.series(0).values.first(), 1.0);
    EXPECT_DOUBLE_EQ(series.series(2).values.first(), 3.0);
}

TEST(CsvSeries, IgnoresNonNumericLines)
{
    CsvSeries series;
    // Устройства шлют сообщения о состоянии вперемешку с данными; считать их ошибкой
    // значило бы засорять вывод жалобами на нормальную работу.
    EXPECT_FALSE(series.feed(QStringLiteral("boot ok"), 0));
    EXPECT_FALSE(series.feed(QStringLiteral("1,oops,3"), 0));
    EXPECT_EQ(series.seriesCount(), 0);
}

TEST(CsvSeries, GrowsToTheWidestLine)
{
    CsvSeries series;
    series.feed(QStringLiteral("1"), 0);
    series.feed(QStringLiteral("1,2,3"), 0);

    EXPECT_EQ(series.seriesCount(), 3);
}

TEST(CsvSeries, DropsOldestBeyondCapacity)
{
    CsvSeries series;
    series.setCapacity(3);
    for (int i = 0; i < 10; ++i)
        series.feed(QString::number(i), 0);

    ASSERT_EQ(series.seriesCount(), 1);
    ASSERT_EQ(series.series(0).values.size(), 3);
    // Показывается хвост потока: неограниченный рост съел бы память за часы работы.
    EXPECT_DOUBLE_EQ(series.series(0).values.first(), 7.0);
    EXPECT_DOUBLE_EQ(series.series(0).values.last(), 9.0);
}

TEST(CsvSeries, ShrinkingCapacityTrimsImmediately)
{
    CsvSeries series;
    for (int i = 0; i < 10; ++i)
        series.feed(QString::number(i), 0);
    series.setCapacity(2);

    EXPECT_EQ(series.series(0).values.size(), 2);
}

TEST(CsvSeries, RangeCoversAllSeries)
{
    CsvSeries series;
    series.feed(QStringLiteral("1,-5"), 0);
    series.feed(QStringLiteral("9,0"), 0);

    double lo = 0.0;
    double hi = 0.0;
    series.range(&lo, &hi);
    EXPECT_DOUBLE_EQ(lo, -5.0);
    EXPECT_DOUBLE_EQ(hi, 9.0);
}

TEST(CsvSeries, ConstantSignalGetsANonZeroRange)
{
    CsvSeries series;
    series.feed(QStringLiteral("4"), 0);
    series.feed(QStringLiteral("4"), 0);

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
    EXPECT_TRUE(series.feed(QStringLiteral("1;2"), 0));
    EXPECT_EQ(series.seriesCount(), 2);
}

TEST(CsvSeries, ClearDropsEverything)
{
    CsvSeries series;
    series.feed(QStringLiteral("1,2"), 0);
    series.clear();
    EXPECT_EQ(series.seriesCount(), 0);
}

// --- Таблица рядов, статистика и выгрузка ---------------------------------------------

TEST(CsvSeries, HiddenSeriesDoesNotAffectRange)
{
    CsvSeries series;
    series.feed(QStringLiteral("1,1000"), 0);
    series.feed(QStringLiteral("2,2000"), 0);

    double lo = 0.0;
    double hi = 0.0;
    series.range(&lo, &hi);
    EXPECT_DOUBLE_EQ(hi, 2000.0);

    // Выброс выключают ровно затем, чтобы он перестал задавать масштаб.
    series.setSeriesVisible(1, false);
    series.range(&lo, &hi);
    EXPECT_DOUBLE_EQ(hi, 2.0);
}

TEST(CsvSeries, StatisticsCoverTheWindow)
{
    CsvSeries series;
    series.feed(QStringLiteral("1,0"), 0);
    series.feed(QStringLiteral("3,0"), 0);
    series.feed(QStringLiteral("5,0"), 0);

    EXPECT_DOUBLE_EQ(series.minimumOf(0), 1.0);
    EXPECT_DOUBLE_EQ(series.maximumOf(0), 5.0);
    EXPECT_DOUBLE_EQ(series.averageOf(0), 3.0);
}

TEST(CsvSeries, StatisticsOfEmptySeriesAreZero)
{
    CsvSeries series;
    EXPECT_DOUBLE_EQ(series.minimumOf(0), 0.0);
    EXPECT_DOUBLE_EQ(series.averageOf(5), 0.0);
}

TEST(CsvSeries, CsvExportHasHeaderAndTimeColumn)
{
    CsvSeries series;
    series.feed(QStringLiteral("1,2"), 0);
    series.feed(QStringLiteral("3,4"), 1'000'000);   // +1 мс

    const QStringList lines = series.toCsv().split(u'\n');
    ASSERT_EQ(lines.size(), 3);
    EXPECT_TRUE(lines.at(0).startsWith(QStringLiteral("time_ms,")));
    // Время отсчитывается от первой точки, а не от запуска канала.
    EXPECT_TRUE(lines.at(1).startsWith(QStringLiteral("0,")));
    EXPECT_TRUE(lines.at(2).startsWith(QStringLiteral("1,")));
}

TEST(CsvSeries, ExportOfEmptyModelIsEmpty)
{
    CsvSeries series;
    EXPECT_TRUE(series.toCsv().isEmpty());
}

TEST(CsvSeries, SeriesGetDistinctDefaultColours)
{
    CsvSeries series;
    series.feed(QStringLiteral("1,2,3"), 0);
    EXPECT_NE(series.series(0).color, series.series(1).color);
    EXPECT_NE(series.series(1).color, series.series(2).color);
}

TEST(CsvSeries, XAxisRejectsOutOfRange)
{
    CsvSeries series;
    series.feed(QStringLiteral("1,2"), 0);

    series.setXAxisSeries(1);
    EXPECT_EQ(series.xAxisSeries(), 1);

    // Несуществующая колонка означает «по времени», а не отказ с прежним значением.
    series.setXAxisSeries(7);
    EXPECT_EQ(series.xAxisSeries(), -1);
}
