/**
 * \file test_sample_buffer.cpp
 * \brief Тесты хранилища отсчётов: инвариант ширины, кольцо, сводки.
 */
#include <spotty/data/SampleBuffer.h>

#include <gtest/gtest.h>

#include <QtMath>

#include <cmath>

using namespace spotty;

namespace {

/// \brief Добавить отсчёт из списка значений — в тестах так читается заметно легче.
bool append(SampleBuffer &buffer, qint64 ns, std::initializer_list<double> values)
{
    const QList<double> list(values);
    return buffer.append(ns, list.constData(), int(list.size()));
}

/**
 * \brief Число колонок из режима «Growing column count» плагина signalgen.
 *
 * Треугольник 1→6→1: именно смена ширины в **обе** стороны и порождала прежнюю путаницу.
 */
int growingColumnCount(int step, int maxColumns)
{
    constexpr int kStepSamples = 15;
    const int phase = (step / kStepSamples) % (2 * maxColumns);
    return phase < maxColumns ? phase + 1 : 2 * maxColumns - phase;
}

} // namespace

TEST(SampleBuffer, ShortLineFillsMissingWithNaN)
{
    // Прежде хвостовые ряды не получали ничего и отставали навсегда.
    SampleBuffer buffer;
    append(buffer, 0, {1.0, 2.0, 3.0});
    append(buffer, 1, {4.0, 5.0});

    ASSERT_EQ(buffer.columnCount(), 3);
    ASSERT_EQ(buffer.sampleCount(), 2);
    EXPECT_DOUBLE_EQ(buffer.at(1, 1), 5.0);
    EXPECT_TRUE(std::isnan(buffer.at(1, 2)));
}

TEST(SampleBuffer, LongLineGrowsColumnsAndBackfillsNaN)
{
    // Колонка, появившаяся посреди потока, не должна получать собственной длины.
    SampleBuffer buffer;
    append(buffer, 0, {1.0});
    append(buffer, 1, {2.0, 20.0, 200.0});

    ASSERT_EQ(buffer.columnCount(), 3);
    ASSERT_EQ(buffer.sampleCount(), 2);
    EXPECT_TRUE(std::isnan(buffer.at(0, 1)));
    EXPECT_TRUE(std::isnan(buffer.at(0, 2)));
    EXPECT_DOUBLE_EQ(buffer.at(1, 2), 200.0);
}

TEST(SampleBuffer, GrowingColumnCountOscillation)
{
    // Главная регрессия: воспроизводит режим signalgen, качающий ширину вверх и вниз.
    // Проверяется не результат в конце, а инвариант после каждого добавления.
    SampleBuffer buffer;
    constexpr int kMaxColumns = 6;
    int widest = 0;

    for (int step = 0; step < 400; ++step) {
        const int columns = growingColumnCount(step, kMaxColumns);
        QList<double> values;
        for (int c = 0; c < columns; ++c)
            values.append(double(step) + double(c) / 10.0);

        ASSERT_TRUE(buffer.append(qint64(step), values.constData(), columns));
        widest = qMax(widest, columns);

        ASSERT_EQ(buffer.columnCount(), widest) << "шаг " << step;
        ASSERT_EQ(buffer.sampleCount(), qMin(step + 1, buffer.capacity())) << "шаг " << step;

        const int last = buffer.sampleCount() - 1;
        for (int c = 0; c < buffer.columnCount(); ++c) {
            const double value = buffer.at(last, c);
            if (c < columns)
                ASSERT_DOUBLE_EQ(value, double(step) + double(c) / 10.0) << "шаг " << step;
            else
                ASSERT_TRUE(std::isnan(value)) << "шаг " << step << ", колонка " << c;
        }
    }
}

TEST(SampleBuffer, TimestampsNeverDriftFromSamples)
{
    // Прежде метки времени росли и подрезались отдельно от рядов, и подпись оси описывала
    // отрезок, против которого не построен ни один ряд.
    SampleBuffer buffer;
    buffer.setCapacity(10);

    for (int i = 0; i < 50; ++i) {
        const int columns = (i % 3) + 1;
        QList<double> values(columns, double(i));
        buffer.append(qint64(i) * 1000, values.constData(), columns);
        ASSERT_EQ(buffer.timestamp(buffer.sampleCount() - 1), qint64(i) * 1000);
    }

    EXPECT_EQ(buffer.sampleCount(), 10);
    EXPECT_EQ(buffer.timestamp(0), 40 * 1000);
    EXPECT_EQ(buffer.timestamp(9), 49 * 1000);
}

TEST(SampleBuffer, BackwardsTimestampIsClampedNotDropped)
{
    // Устройство иногда переставляет строки при переполнении очереди. Данные при этом
    // настоящие, а вот немонотонная ось сложила бы кривую саму на себя.
    SampleBuffer buffer;
    append(buffer, 1000, {1.0});
    append(buffer, 500, {2.0});

    ASSERT_EQ(buffer.sampleCount(), 2);
    EXPECT_EQ(buffer.timestamp(1), 1000);
    EXPECT_DOUBLE_EQ(buffer.at(1, 0), 2.0);
}

TEST(SampleBuffer, CapacityTrimsWholeRows)
{
    SampleBuffer buffer;
    buffer.setCapacity(3);
    for (int i = 0; i < 10; ++i)
        append(buffer, qint64(i), {double(i), double(i) * 2});

    ASSERT_EQ(buffer.sampleCount(), 3);
    EXPECT_DOUBLE_EQ(buffer.at(0, 0), 7.0);
    EXPECT_DOUBLE_EQ(buffer.at(0, 1), 14.0);
    EXPECT_DOUBLE_EQ(buffer.at(2, 0), 9.0);
}

TEST(SampleBuffer, ShrinkingCapacityTrimsImmediately)
{
    SampleBuffer buffer;
    buffer.setCapacity(100);
    for (int i = 0; i < 50; ++i)
        append(buffer, qint64(i), {double(i)});

    buffer.setCapacity(10);
    ASSERT_EQ(buffer.sampleCount(), 10);
    // Оставлен хвост: свежие данные нужнее старых.
    EXPECT_DOUBLE_EQ(buffer.at(0, 0), 40.0);
    EXPECT_DOUBLE_EQ(buffer.at(9, 0), 49.0);
}

TEST(SampleBuffer, RingWrapsWithoutMovingData)
{
    // Три полных оборота: если бы кольцо где-то теряло голову, порядок бы поехал.
    SampleBuffer buffer;
    buffer.setCapacity(4);
    for (int i = 0; i < 13; ++i)
        append(buffer, qint64(i), {double(i)});

    ASSERT_EQ(buffer.sampleCount(), 4);
    for (int row = 0; row < 4; ++row)
        EXPECT_DOUBLE_EQ(buffer.at(row, 0), double(9 + row));
}

TEST(SampleBuffer, ClearSamplesKeepsColumnCount)
{
    // Пункт 7 владельца: очистка роняет накопленное, но не состав колонок.
    SampleBuffer buffer;
    append(buffer, 0, {1.0, 2.0, 3.0});
    buffer.clearSamples();

    EXPECT_EQ(buffer.sampleCount(), 0);
    EXPECT_EQ(buffer.columnCount(), 3);
}

TEST(SampleBuffer, ResetDropsColumnsToo)
{
    SampleBuffer buffer;
    append(buffer, 0, {1.0, 2.0});
    buffer.reset();

    EXPECT_EQ(buffer.sampleCount(), 0);
    EXPECT_EQ(buffer.columnCount(), 0);
}

TEST(SampleBuffer, ClearColumnBlanksOnlyThatColumn)
{
    SampleBuffer buffer;
    append(buffer, 0, {1.0, 2.0, 3.0});
    append(buffer, 1, {4.0, 5.0, 6.0});

    buffer.clearColumn(1);

    EXPECT_DOUBLE_EQ(buffer.at(0, 0), 1.0);
    EXPECT_TRUE(std::isnan(buffer.at(0, 1)));
    EXPECT_DOUBLE_EQ(buffer.at(0, 2), 3.0);
    EXPECT_DOUBLE_EQ(buffer.at(1, 2), 6.0);
    // Строки на месте: очищено значение, а не отсчёт.
    EXPECT_EQ(buffer.sampleCount(), 2);
}

TEST(SampleBuffer, StatsIgnoreNaN)
{
    SampleBuffer buffer;
    append(buffer, 0, {1.0, 10.0});
    append(buffer, 1, {3.0});          // вторая колонка получит NaN
    append(buffer, 2, {5.0, 30.0});

    const SampleBuffer::ColumnStats first = buffer.stats(0);
    EXPECT_EQ(first.finiteCount, 3);
    EXPECT_DOUBLE_EQ(first.minimum, 1.0);
    EXPECT_DOUBLE_EQ(first.maximum, 5.0);
    EXPECT_DOUBLE_EQ(first.mean, 3.0);

    const SampleBuffer::ColumnStats second = buffer.stats(1);
    EXPECT_EQ(second.finiteCount, 2);
    EXPECT_DOUBLE_EQ(second.mean, 20.0);
}

TEST(SampleBuffer, StatsOfEmptyColumnReportNothingRatherThanZero)
{
    // Ноль в колонке «Мин» читался бы как измеренное значение. Признак отдельный.
    SampleBuffer buffer;
    append(buffer, 0, {1.0});
    buffer.clearColumn(0);

    EXPECT_EQ(buffer.stats(0).finiteCount, 0);
}

TEST(SampleBuffer, StatsAreInvalidatedByAppend)
{
    SampleBuffer buffer;
    append(buffer, 0, {1.0});
    ASSERT_DOUBLE_EQ(buffer.stats(0).maximum, 1.0);

    append(buffer, 1, {9.0});
    EXPECT_DOUBLE_EQ(buffer.stats(0).maximum, 9.0);
}

TEST(SampleBuffer, TooManyColumnsIsRejected)
{
    // Устройство, приславшее втрое больше полей, потеряло кадровую синхронизацию. Обрезка
    // молча выдумала бы данные, поэтому строка отвергается целиком.
    SampleBuffer buffer;
    buffer.setColumnLimit(4);

    QList<double> wide(9, 1.0);
    EXPECT_FALSE(buffer.append(0, wide.constData(), int(wide.size())));
    EXPECT_EQ(buffer.sampleCount(), 0);
    EXPECT_EQ(buffer.columnCount(), 0);
}

TEST(SampleBuffer, EmptyAppendIsRefused)
{
    SampleBuffer buffer;
    EXPECT_FALSE(buffer.append(0, nullptr, 0));
    EXPECT_EQ(buffer.sampleCount(), 0);
}

TEST(SampleBuffer, ToCsvHasOneRowPerSampleAndEmptyForMissing)
{
    SampleBuffer buffer;
    append(buffer, 0, {1.0, 2.0});
    append(buffer, 5'000'000, {3.0});

    const QStringList lines = buffer.toCsv({QStringLiteral("alpha"), QStringLiteral("bravo")})
                                  .split(u'\n');

    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines.at(0), QStringLiteral("time_ms,alpha,bravo"));
    EXPECT_EQ(lines.at(1), QStringLiteral("0,1,2"));
    // Пропуск выходит пустым полем: «nan» половина читателей CSV принимает за текст.
    EXPECT_EQ(lines.at(2), QStringLiteral("5,3,"));
}

TEST(SampleBuffer, CopyColumnReadsContiguously)
{
    SampleBuffer buffer;
    for (int i = 0; i < 5; ++i)
        append(buffer, qint64(i), {double(i), double(i) * 10});

    double out[3] = {};
    buffer.copyColumn(1, 1, 3, out);

    EXPECT_DOUBLE_EQ(out[0], 10.0);
    EXPECT_DOUBLE_EQ(out[1], 20.0);
    EXPECT_DOUBLE_EQ(out[2], 30.0);
}
