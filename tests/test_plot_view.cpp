/**
 * \file test_plot_view.cpp
 * \brief Тесты состояния вида: окно, слежение, масштаб, активная ось и группа.
 */
#include <spotty/data/PlotTransform.h>
#include <spotty/data/PlotViewState.h>

#include <gtest/gtest.h>

using namespace spotty;

namespace {

constexpr qint64 kSecond = 1'000'000'000LL;

PlotSeries plainSeries()
{
    PlotSeries series;
    series.name = QStringLiteral("alpha");
    return series;
}

} // namespace

TEST(PlotViewState, FollowKeepsTheWindowAtTheEnd)
{
    PlotViewState view;
    view.setWindowDuration(10 * kSecond);

    view.clampTo(0, 100 * kSecond);

    EXPECT_TRUE(view.following());
    EXPECT_EQ(view.windowTo(), 100 * kSecond);
    EXPECT_EQ(view.windowDuration(), 10 * kSecond);
}

TEST(PlotViewState, PanningAwayFromTheEndStopsFollowing)
{
    // Иначе окно тут же уехало бы обратно, и потащить график назад было бы невозможно.
    PlotViewState view;
    view.setWindowDuration(10 * kSecond);
    view.clampTo(0, 100 * kSecond);

    view.panBy(-30 * kSecond);

    EXPECT_FALSE(view.following());
    EXPECT_EQ(view.windowTo(), 70 * kSecond);
}

TEST(PlotViewState, PanningBackToTheEndResumesFollowing)
{
    // Тот же жест, что «Follow output» в терминале: доводка до края возвращает слежение.
    PlotViewState view;
    view.setWindowDuration(10 * kSecond);
    view.clampTo(0, 100 * kSecond);
    view.panBy(-30 * kSecond);
    ASSERT_FALSE(view.following());

    view.panBy(30 * kSecond);
    view.clampTo(0, 100 * kSecond);

    EXPECT_TRUE(view.following());
}

TEST(PlotViewState, WindowIsClampedToTheBuffer)
{
    // За краями буфера смотреть нечего — там заведомо пусто.
    PlotViewState view;
    view.setWindowDuration(10 * kSecond);
    view.setFollowing(false);
    view.setWindow(500 * kSecond, 510 * kSecond);

    view.clampTo(0, 100 * kSecond);

    EXPECT_LE(view.windowTo(), 100 * kSecond);
}

TEST(PlotViewState, ZoomXAnchorsAtTheCursor)
{
    // Точка под курсором остаётся на месте: иначе приближение уводит из-под указателя то
    // самое место, ради которого его и делают.
    PlotViewState view;
    view.setFollowing(false);
    view.setWindow(0, 100 * kSecond);

    const qint64 anchor = 25 * kSecond;
    const double fractionBefore =
        double(anchor - view.windowFrom()) / double(view.windowDuration());

    view.zoomX(0.5, anchor);

    const double fractionAfter =
        double(anchor - view.windowFrom()) / double(view.windowDuration());

    EXPECT_NEAR(fractionBefore, fractionAfter, 1e-6);
    EXPECT_EQ(view.windowDuration(), 50 * kSecond);
}

TEST(PlotViewState, ZoomIsClampedAtBothEnds)
{
    PlotViewState view;
    view.setFollowing(false);
    view.setWindow(0, 10 * kSecond);

    for (int i = 0; i < 100; ++i)
        view.zoomX(0.5, 0);
    EXPECT_GT(view.windowDuration(), 0);

    for (int i = 0; i < 200; ++i)
        view.zoomX(2.0, 0);
    EXPECT_LE(view.windowDuration(), 86'400'000'000'000LL);
}

TEST(PlotViewState, ActiveSeriesIsForcedIntoTheSelectionGroup)
{
    // Инвариант, снимающий противоречие между двумя механизмами: слева подписана шкала
    // активного ряда, а у группы шкала общая. Если бы активный был вне группы, подпись не
    // соответствовала бы ни одной нарисованной кривой.
    PlotViewState view;
    view.setActiveSeries(5);

    view.setSelectionGroup({1, 2});

    EXPECT_TRUE(view.hasScaleGroup());
    EXPECT_EQ(view.activeSeries(), 1);
}

TEST(PlotViewState, ActiveSeriesSurvivesWhenItIsAlreadyInTheGroup)
{
    PlotViewState view;
    view.setActiveSeries(2);
    view.setSelectionGroup({1, 2, 3});

    EXPECT_EQ(view.activeSeries(), 2);
}

TEST(PlotViewState, SelectionOfOneIsNotAGroup)
{
    // Один выделенный ряд — это «сделать активным», а не «свести на общую шкалу».
    PlotViewState view;
    view.setActiveSeries(7);
    view.setSelectionGroup({3});

    EXPECT_FALSE(view.hasScaleGroup());
    EXPECT_EQ(view.activeSeries(), 7);
}

TEST(PlotScalesTest, CustomRangeOverridesGroupAndAuto)
{
    // Пользователь ввёл эти числа руками — их не перебивает ничто.
    PlotSeries series = plainSeries();
    series.hasCustomRange = true;
    series.customMinimum = -5.0;
    series.customMaximum = 5.0;

    const auto range = PlotScales::resolve(series, {0.0, 100.0, true}, {0.0, 200.0, true});

    EXPECT_DOUBLE_EQ(range.minimum, -5.0);
    EXPECT_DOUBLE_EQ(range.maximum, 5.0);
}

TEST(PlotScalesTest, GroupRangeOverridesOwn)
{
    const auto range = PlotScales::resolve(plainSeries(), {0.0, 1.0, true},
                                           {-10.0, 10.0, true});

    // С запасом по краям, поэтому шире объединения, но вокруг него.
    EXPECT_LT(range.minimum, -10.0);
    EXPECT_GT(range.maximum, 10.0);
}

TEST(PlotScalesTest, OwnRangeIsUsedWithoutAGroup)
{
    const auto range = PlotScales::resolve(plainSeries(), {2.0, 4.0, true}, {});

    EXPECT_LT(range.minimum, 2.0);
    EXPECT_GT(range.maximum, 4.0);
}

TEST(PlotScalesTest, ConstantSignalGetsANonZeroRange)
{
    // Иначе высота шкалы нулевая, и деление на неё даёт бесконечность.
    const auto range = PlotScales::resolve(plainSeries(), {3.0, 3.0, true}, {});

    EXPECT_LT(range.minimum, range.maximum);
}

TEST(PlotScalesTest, MergeSkipsInvalidRanges)
{
    const auto merged = PlotScales::merge({1.0, 2.0, true}, {});
    EXPECT_DOUBLE_EQ(merged.maximum, 2.0);

    const auto both = PlotScales::merge({1.0, 2.0, true}, {-1.0, 5.0, true});
    EXPECT_DOUBLE_EQ(both.minimum, -1.0);
    EXPECT_DOUBLE_EQ(both.maximum, 5.0);
}

TEST(XTransformTest, MapsTimeToTheFieldAndBack)
{
    const XTransform transform{0, 100 * kSecond, 10.0, 200.0};

    EXPECT_DOUBLE_EQ(transform.xOf(0), 10.0);
    EXPECT_DOUBLE_EQ(transform.xOf(100 * kSecond), 210.0);
    EXPECT_EQ(transform.timeAt(110.0), 50 * kSecond);
}

TEST(XTransformTest, ColumnOfClampsTheRightEdge)
{
    // Правый край иначе попадал бы в колонку номер columns, которой не существует, и
    // последний отсчёт окна выпадал бы из результата.
    const XTransform transform{0, 100 * kSecond, 0.0, 100.0};

    EXPECT_EQ(transform.columnOf(0, 100), 0);
    EXPECT_EQ(transform.columnOf(100 * kSecond, 100), 99);
    EXPECT_EQ(transform.columnOf(200 * kSecond, 100), -1);
}

TEST(YScaleTest, MapsValueToTheFieldAndBack)
{
    const YScale scale{0.0, 10.0, 0.0, 100.0};

    // Ноль внизу, десятка наверху: экранная вертикаль растёт вниз.
    EXPECT_DOUBLE_EQ(scale.yOf(0.0), 100.0);
    EXPECT_DOUBLE_EQ(scale.yOf(10.0), 0.0);
    EXPECT_DOUBLE_EQ(scale.valueAt(50.0), 5.0);
}
