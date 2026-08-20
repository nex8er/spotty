/**
 * \file test_plot_model.cpp
 * \brief Тесты модели плоттера: оформление рядов переживает очистку.
 */
#include <spotty/data/PlotModel.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace spotty;

TEST(PlotModel, ParsesNumericLineIntoSeries)
{
    PlotModel model;
    EXPECT_TRUE(model.feed(QStringLiteral("1,2,3"), 0));

    ASSERT_EQ(model.seriesCount(), 3);
    EXPECT_DOUBLE_EQ(model.samples().at(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(model.samples().at(0, 2), 3.0);
}

TEST(PlotModel, SeriesGetNatoNamesAndDistinctColours)
{
    PlotModel model;
    model.feed(QStringLiteral("1,2,3"), 0);

    EXPECT_EQ(model.series(0).name, QStringLiteral("alpha"));
    EXPECT_EQ(model.series(1).name, QStringLiteral("bravo"));
    EXPECT_NE(model.series(0).color, model.series(1).color);
}

TEST(PlotModel, IgnoresNonNumericLines)
{
    PlotModel model;
    EXPECT_FALSE(model.feed(QStringLiteral("boot ok"), 0));
    EXPECT_FALSE(model.feed(QStringLiteral("1,oops,3"), 0));
    EXPECT_EQ(model.seriesCount(), 0);
}

TEST(PlotModel, GrowsToTheWidestLine)
{
    PlotModel model;
    model.feed(QStringLiteral("1"), 0);
    model.feed(QStringLiteral("1,2,3"), 1);

    EXPECT_EQ(model.seriesCount(), 3);
    EXPECT_EQ(model.series(2).name, QStringLiteral("charlie"));
}

TEST(PlotModel, ClearKeepsSeriesMetadata)
{
    // Пункт 7 владельца, и главная причина, по которой оформление лежит отдельно от
    // значений: прежняя clear() роняла цвета, имена, видимость и выбор оси X разом.
    PlotModel model;
    model.feed(QStringLiteral("1,2,3"), 0);
    model.setSeriesColor(1, 0xFF00FF00);
    model.setSeriesName(1, QStringLiteral("voltage"));
    model.setSeriesVisible(2, false);
    model.setSeriesRange(0, true, -5.0, 5.0);
    model.setXAxisSeries(0);

    model.clearSamples();

    ASSERT_EQ(model.seriesCount(), 3);
    EXPECT_EQ(model.samples().sampleCount(), 0);
    EXPECT_EQ(model.series(1).color, 0xFF00FF00u);
    EXPECT_EQ(model.series(1).name, QStringLiteral("voltage"));
    EXPECT_FALSE(model.series(2).visible);
    EXPECT_TRUE(model.series(0).hasCustomRange);
    EXPECT_EQ(model.xAxisSeries(), 0);
}

TEST(PlotModel, HeaderRowRenamesSeries)
{
    PlotModel model;
    model.feed(QStringLiteral("1,2,3"), 0);
    EXPECT_FALSE(model.feed(QStringLiteral("time,volt,curr"), 1));

    EXPECT_EQ(model.series(1).name, QStringLiteral("volt"));
    // Заголовок отсчёта не даёт.
    EXPECT_EQ(model.samples().sampleCount(), 1);
}

TEST(PlotModel, HeaderDoesNotOverwriteCustomNames)
{
    // Повторяющийся баннер устройства не должен отменять правку пользователя.
    PlotModel model;
    model.feed(QStringLiteral("1,2,3"), 0);
    model.setSeriesName(1, QStringLiteral("моё имя"));
    model.feed(QStringLiteral("time,volt,curr"), 1);

    EXPECT_EQ(model.series(1).name, QStringLiteral("моё имя"));
    EXPECT_EQ(model.series(2).name, QStringLiteral("curr"));
}

TEST(PlotModel, EmptyNameRestoresTheDefault)
{
    // Стерев имя, пользователь получил бы безымянную строку без пути назад.
    PlotModel model;
    model.feed(QStringLiteral("1,2"), 0);
    model.setSeriesName(0, QStringLiteral("custom"));
    model.setSeriesName(0, QString());

    EXPECT_EQ(model.series(0).name, QStringLiteral("alpha"));
    EXPECT_FALSE(model.series(0).nameIsCustom);
}

TEST(PlotModel, ResetSeriesConfigurationKeepsReportedNames)
{
    // Профиль может временно подменить подписи полей своими. Сброс обязан вернуть именно
    // заголовок устройства, а не alpha/bravo, если заголовок уже был получен.
    PlotModel model;
    model.feed(QStringLiteral("1,2"), 0);
    model.feed(QStringLiteral("voltage,current"), 1);
    model.setSeriesName(0, QStringLiteral("Vbat"));
    model.setSeriesColor(0, 0xFF00FF00);
    // Оба от умолчания: по умолчанию виден только индекс 0 — здесь наоборот.
    model.setSeriesVisible(0, false);
    model.setSeriesVisible(1, true);
    model.setSeriesRange(0, true, -5.0, 5.0);

    model.resetSeriesConfiguration();

    EXPECT_EQ(model.series(0).name, QStringLiteral("voltage"));
    EXPECT_EQ(model.series(1).name, QStringLiteral("current"));
    EXPECT_FALSE(model.series(0).nameIsCustom);
    // Умолчание — виден только первый ряд.
    EXPECT_TRUE(model.series(0).visible);
    EXPECT_FALSE(model.series(1).visible);
    EXPECT_FALSE(model.series(0).hasCustomRange);

    PlotModel defaults;
    defaults.feed(QStringLiteral("1,2"), 0);
    EXPECT_EQ(model.series(0).color, defaults.series(0).color);
    EXPECT_EQ(model.series(1).color, defaults.series(1).color);
}

TEST(PlotModel, HeaderBeforeDataNamesNewSeries)
{
    // Устройство часто печатает заголовок ещё до первого отсчёта. Его нельзя забыть,
    // иначе у сброшенного профиля не будет пути обратно к собственным именам потока.
    PlotModel model;
    EXPECT_FALSE(model.feed(QStringLiteral("voltage,current"), 0));
    EXPECT_TRUE(model.feed(QStringLiteral("1,2"), 1));

    EXPECT_EQ(model.series(0).name, QStringLiteral("voltage"));
    EXPECT_EQ(model.series(1).name, QStringLiteral("current"));
}

TEST(PlotModel, CustomRangeIsNormalized)
{
    // Перевёрнутый или вырожденный отрезок дал бы деление на ноль в преобразовании координат.
    PlotModel model;
    model.feed(QStringLiteral("1"), 0);

    model.setSeriesRange(0, true, 10.0, -10.0);
    EXPECT_DOUBLE_EQ(model.series(0).customMinimum, -10.0);
    EXPECT_DOUBLE_EQ(model.series(0).customMaximum, 10.0);

    model.setSeriesRange(0, true, 3.0, 3.0);
    EXPECT_LT(model.series(0).customMinimum, model.series(0).customMaximum);
}

TEST(PlotModel, XAxisRejectsOutOfRange)
{
    PlotModel model;
    model.feed(QStringLiteral("1,2"), 0);

    model.setXAxisSeries(5);
    EXPECT_EQ(model.xAxisSeries(), -1);

    model.setXAxisSeries(1);
    EXPECT_EQ(model.xAxisSeries(), 1);
}

TEST(PlotModel, SeparatorIsConfigurable)
{
    PlotModel model;
    model.setSeparator(u';');
    EXPECT_TRUE(model.feed(QStringLiteral("1;2;3"), 0));
    EXPECT_EQ(model.seriesCount(), 3);
}

TEST(PlotModel, CapacityShrinksTheBuffer)
{
    PlotModel model;
    model.setCapacity(4);
    for (int i = 0; i < 10; ++i)
        model.feed(QString::number(i), qint64(i));

    EXPECT_EQ(model.samples().sampleCount(), 4);
    EXPECT_DOUBLE_EQ(model.samples().at(0, 0), 6.0);
}

TEST(PlotModel, OverwideLineIsCountedAsRejected)
{
    PlotModel model;
    model.setCapacity(10);

    QStringList fields;
    for (int i = 0; i < 200; ++i)
        fields << QString::number(i);

    EXPECT_FALSE(model.feed(fields.join(u','), 0));
    EXPECT_EQ(model.rejectedCount(), 1);
    EXPECT_EQ(model.seriesCount(), 0);
}

TEST(PlotModel, CsvExportUsesSeriesNames)
{
    PlotModel model;
    model.feed(QStringLiteral("1,2"), 0);
    model.setSeriesName(1, QStringLiteral("volt"));

    const QStringList lines = model.toCsv().split(u'\n');
    ASSERT_GE(lines.size(), 2);
    EXPECT_EQ(lines.at(0), QStringLiteral("time_ms,alpha,volt"));
}
