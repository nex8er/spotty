/**
 * \file test_decimator.cpp
 * \brief Тесты прореживания: корзины по времени, разрывы, накопление.
 */
#include <spotty/data/Decimator.h>

#include <gtest/gtest.h>

#include <QtMath>

using namespace spotty;

namespace {

/// \brief Секунды в наносекундах — метки времени в буфере именно такие.
constexpr qint64 kSecond = 1'000'000'000LL;

void append(SampleBuffer &buffer, qint64 ns, double value)
{
    buffer.append(ns, &value, 1);
}

XTransform windowOf(qint64 from, qint64 to)
{
    return XTransform{from, to, 0.0, 100.0};
}

} // namespace

TEST(Decimator, FewerSamplesThanColumnsKeepsEveryPoint)
{
    SampleBuffer buffer;
    for (int i = 0; i < 5; ++i)
        append(buffer, qint64(i) * kSecond, double(i));

    const auto result = Decimator::reduce(buffer, 0, windowOf(0, 4 * kSecond), 100);

    EXPECT_EQ(result.columns.size(), 5);
    EXPECT_DOUBLE_EQ(result.visible.minimum, 0.0);
    EXPECT_DOUBLE_EQ(result.visible.maximum, 4.0);
}

TEST(Decimator, BucketsFollowTimeNotIndex)
{
    // Структурная проверка того, что ряды больше не лежат в разных масштабах: тысяча
    // отсчётов в первой десятой части окна обязана занять примерно десятую часть колонок,
    // а не растянуться на всю ширину, как было при нумерации по индексу.
    SampleBuffer buffer;
    buffer.setCapacity(2000);

    for (int i = 0; i < 1000; ++i)
        append(buffer, qint64(i) * kSecond / 1000, 1.0); // первая секунда
    for (int i = 0; i < 10; ++i)
        append(buffer, (1 + qint64(i)) * kSecond, 2.0);  // следующие десять секунд

    const auto result = Decimator::reduce(buffer, 0, windowOf(0, 10 * kSecond), 100);

    int burstColumns = 0;
    for (const auto &column : result.columns) {
        if (qFuzzyCompare(column.maximum, 1.0))
            ++burstColumns;
    }

    // Пачка занимает первую десятую часть окна, то есть около десяти колонок из ста.
    EXPECT_LE(burstColumns, 15);
    EXPECT_GE(burstColumns, 5);
}

TEST(Decimator, MinMaxPerColumnPreservesSpikes)
{
    // Одиночный выброс среди сотни ровных значений обязан уцелеть: ради выбросов на график
    // чаще всего и смотрят.
    SampleBuffer buffer;
    buffer.setCapacity(1000);
    for (int i = 0; i < 500; ++i)
        append(buffer, qint64(i) * kSecond / 100, i == 250 ? 99.0 : 1.0);

    const auto result = Decimator::reduce(buffer, 0, windowOf(0, 5 * kSecond), 10);

    ASSERT_FALSE(result.columns.isEmpty());
    EXPECT_DOUBLE_EQ(result.visible.maximum, 99.0);
}

TEST(Decimator, NanSplitsRuns)
{
    // Соединив соседей через пропуск, мы показали бы данные, которых устройство не слало.
    SampleBuffer buffer;
    append(buffer, 0, 1.0);
    append(buffer, kSecond, 2.0);
    append(buffer, 2 * kSecond, qQNaN());
    append(buffer, 3 * kSecond, 4.0);

    const auto result = Decimator::reduce(buffer, 0, windowOf(0, 3 * kSecond), 100);

    EXPECT_EQ(result.runStarts.size(), 2);
    EXPECT_EQ(result.columns.size(), 3);
}

TEST(Decimator, AllNanProducesNothing)
{
    SampleBuffer buffer;
    append(buffer, 0, qQNaN());
    append(buffer, kSecond, qQNaN());

    const auto result = Decimator::reduce(buffer, 0, windowOf(0, kSecond), 100);

    EXPECT_TRUE(result.columns.isEmpty());
    EXPECT_FALSE(result.visible.valid);
}

TEST(Decimator, SamplesOutsideTheWindowAreSkipped)
{
    SampleBuffer buffer;
    for (int i = 0; i < 10; ++i)
        append(buffer, qint64(i) * kSecond, double(i));

    const auto result = Decimator::reduce(buffer, 0, windowOf(3 * kSecond, 5 * kSecond), 100);

    ASSERT_TRUE(result.visible.valid);
    EXPECT_DOUBLE_EQ(result.visible.minimum, 3.0);
    EXPECT_DOUBLE_EQ(result.visible.maximum, 5.0);
}

TEST(Decimator, NeighbouringSamplesExtendTheCurveBeyondWindowEdges)
{
    SampleBuffer buffer;
    for (int i = 0; i < 10; ++i)
        append(buffer, qint64(i) * kSecond, double(i));

    const auto result = Decimator::reduce(buffer, 0, windowOf(3 * kSecond, 5 * kSecond), 100);

    ASSERT_EQ(result.columns.size(), 5);
    EXPECT_EQ(result.columns.first().x, -1);
    EXPECT_EQ(result.columns.last().x, 100);
    // Контекст соединяет кривую, но не раздувает шкалу за счёт невидимых 2 и 6.
    EXPECT_DOUBLE_EQ(result.visible.minimum, 3.0);
    EXPECT_DOUBLE_EQ(result.visible.maximum, 5.0);
}

TEST(Decimator, NeighboursDoNotInventACurveForAnEmptyWindow)
{
    SampleBuffer buffer;
    append(buffer, 0, 1.0);
    append(buffer, 10 * kSecond, 2.0);

    const auto result = Decimator::reduce(buffer, 0, windowOf(4 * kSecond, 6 * kSecond), 100);

    EXPECT_TRUE(result.columns.isEmpty());
    EXPECT_FALSE(result.visible.valid);
}

TEST(Decimator, LongPauseSplitsTheCurve)
{
    SampleBuffer buffer;
    append(buffer, 0, 0.0);
    append(buffer, kSecond, 1.0);
    append(buffer, 2 * kSecond, 2.0);
    append(buffer, 10 * kSecond, 10.0);
    append(buffer, 11 * kSecond, 11.0);

    const auto result = Decimator::reduce(buffer, 0, windowOf(0, 11 * kSecond), 110);

    ASSERT_EQ(result.runStarts.size(), 2);
    EXPECT_EQ(result.runStarts.at(0), 0);
    EXPECT_EQ(result.runStarts.at(1), 3);
}

TEST(Decimator, RegularSparseSamplesStayInOneCurve)
{
    SampleBuffer buffer;
    append(buffer, 0, 0.0);
    append(buffer, 2 * kSecond, 2.0);
    append(buffer, 4 * kSecond, 4.0);
    append(buffer, 6 * kSecond, 6.0);

    const auto result = Decimator::reduce(buffer, 0, windowOf(0, 6 * kSecond), 600);

    EXPECT_EQ(result.runStarts.size(), 1);
}

TEST(Decimator, ReduceReportsVisibleRangeMatchingTheData)
{
    // Контракт «автомасштаб бесплатно»: пределы выпадают из того же прохода.
    SampleBuffer buffer;
    for (int i = 0; i < 100; ++i)
        append(buffer, qint64(i) * kSecond / 10, double(i) - 50.0);

    const auto result = Decimator::reduce(buffer, 0, windowOf(0, 10 * kSecond), 50);

    ASSERT_TRUE(result.visible.valid);
    EXPECT_DOUBLE_EQ(result.visible.minimum, -50.0);
    EXPECT_DOUBLE_EQ(result.visible.maximum, 49.0);
}

TEST(Decimator, RunningSumMatchesNaivePrefixSum)
{
    SampleBuffer buffer;
    for (int i = 1; i <= 10; ++i)
        append(buffer, qint64(i) * kSecond, double(i));

    const auto result = Decimator::reduce(buffer, 0, windowOf(kSecond, 10 * kSecond), 100,
                                          Decimator::Accumulator::RunningSum);

    // 1+2+…+10 == 55
    ASSERT_TRUE(result.visible.valid);
    EXPECT_DOUBLE_EQ(result.visible.maximum, 55.0);
    EXPECT_DOUBLE_EQ(result.visible.minimum, 1.0);
}

TEST(Decimator, RunningSumCountsFromTheBufferStartNotTheWindow)
{
    // «Сколько набежало всего» — ответ про весь сеанс, и прокрутка не должна его менять.
    SampleBuffer buffer;
    for (int i = 1; i <= 10; ++i)
        append(buffer, qint64(i) * kSecond, 1.0);

    const auto result = Decimator::reduce(buffer, 0, windowOf(8 * kSecond, 10 * kSecond), 100,
                                          Decimator::Accumulator::RunningSum);

    ASSERT_TRUE(result.visible.valid);
    EXPECT_DOUBLE_EQ(result.visible.maximum, 10.0);
    EXPECT_DOUBLE_EQ(result.visible.minimum, 8.0);
}

TEST(Decimator, LowerBoundFindsTheWindowStart)
{
    SampleBuffer buffer;
    for (int i = 0; i < 10; ++i)
        append(buffer, qint64(i) * kSecond, double(i));

    EXPECT_EQ(Decimator::lowerBound(buffer, 0), 0);
    EXPECT_EQ(Decimator::lowerBound(buffer, 5 * kSecond), 5);
    EXPECT_EQ(Decimator::lowerBound(buffer, 5 * kSecond - 1), 5);
    EXPECT_EQ(Decimator::lowerBound(buffer, 100 * kSecond), 10);
}

TEST(Decimator, UsesMonotonicDataCounterAsHorizontalCoordinate)
{
    SampleBuffer buffer;
    for (int i = 0; i < 5; ++i) {
        const double values[] = {double(i), double(i) * 100.0};
        buffer.append(qint64(i) * kSecond, values, 2);
    }

    const auto result = Decimator::reduce(
        buffer, 0,
        windowOf(100 * Decimator::kCounterCoordinateScale,
                 300 * Decimator::kCounterCoordinateScale),
        100, Decimator::Accumulator::None, 0, 1);

    ASSERT_TRUE(result.visible.valid);
    EXPECT_DOUBLE_EQ(result.visible.minimum, 1.0);
    EXPECT_DOUBLE_EQ(result.visible.maximum, 3.0);
    EXPECT_EQ(Decimator::lowerBound(buffer, 250 * Decimator::kCounterCoordinateScale, 1), 3);
}

TEST(Decimator, PreservesFractionalDataCounter)
{
    SampleBuffer buffer;
    for (int i = 0; i < 4; ++i) {
        const double values[] = {double(i), 0.25 + double(i) * 0.125};
        buffer.append(qint64(i) * kSecond, values, 2);
    }

    const auto result = Decimator::reduce(
        buffer, 0,
        windowOf(qint64(0.3 * Decimator::kCounterCoordinateScale),
                 qint64(0.6 * Decimator::kCounterCoordinateScale)),
        100, Decimator::Accumulator::None, 0, 1);

    ASSERT_TRUE(result.visible.valid);
    EXPECT_DOUBLE_EQ(result.visible.minimum, 1.0);
    EXPECT_DOUBLE_EQ(result.visible.maximum, 2.0);
}

TEST(Decimator, EmptyBufferProducesNothing)
{
    SampleBuffer buffer;
    const auto result = Decimator::reduce(buffer, 0, windowOf(0, kSecond), 100);
    EXPECT_TRUE(result.columns.isEmpty());
}
