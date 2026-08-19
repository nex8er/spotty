/**
 * \file test_plot_math.cpp
 * \brief Тесты гистограммы и спектра.
 */
#include <spotty/data/PlotMath.h>

#include <gtest/gtest.h>

#include <QtMath>

#include <random>

using namespace spotty;

namespace {

constexpr qint64 kSecond = 1'000'000'000LL;

/// \brief Синусоида: \p count отсчётов с частотой \p rateHz и тоном \p toneHz.
QList<double> sine(int count, double rateHz, double toneHz, double amplitude = 1.0)
{
    QList<double> values;
    values.reserve(count);
    for (int i = 0; i < count; ++i)
        values.append(amplitude * std::sin(2.0 * M_PI * toneHz * double(i) / rateHz));
    return values;
}

/// \brief Номер корзины с наибольшей амплитудой.
int peakBin(const QList<double> &magnitude)
{
    int best = 0;
    for (int i = 1; i < magnitude.size(); ++i) {
        if (magnitude.at(i) > magnitude.at(best))
            best = i;
    }
    return best;
}

} // namespace

TEST(Histogram, BinCountFromFreedmanDiaconis)
{
    // Сотня значений: правило по межквартильному размаху даёт разумный десяток корзин, а
    // не одну и не сотню.
    QList<double> values;
    for (int i = 0; i < 100; ++i)
        values.append(double(i));

    const auto bins = Histogram::bins(values);

    EXPECT_GT(bins.counts.size(), 3);
    EXPECT_LT(bins.counts.size(), 60);
    EXPECT_EQ(bins.total, 100);
}

TEST(Histogram, OutlierDoesNotCollapseThePicture)
{
    // Ради этого и взято правило Фридмана–Дьякониса: по размаху один выброс сложил бы всё
    // остальное в одну корзину.
    QList<double> values;
    for (int i = 0; i < 200; ++i)
        values.append(double(i % 10));
    values.append(100000.0);

    const auto bins = Histogram::bins(values);
    int nonEmpty = 0;
    for (const int count : bins.counts) {
        if (count > 0)
            ++nonEmpty;
    }

    EXPECT_GT(nonEmpty, 1);
}

TEST(Histogram, SmallSampleFallsBackToSturges)
{
    QList<double> values{1.0, 2.0, 3.0, 4.0, 5.0};
    const auto bins = Histogram::bins(values);

    EXPECT_GE(bins.counts.size(), 2);
    EXPECT_LE(bins.counts.size(), 5);
}

TEST(Histogram, AllEqualValuesGiveOneBin)
{
    // Деление на нулевую ширину дало бы бесконечность.
    const auto bins = Histogram::bins({7.0, 7.0, 7.0});

    ASSERT_EQ(bins.counts.size(), 1);
    EXPECT_EQ(bins.counts.first(), 3);
    EXPECT_LT(bins.minimum, bins.maximum);
}

TEST(Histogram, NaNIsSkipped)
{
    const auto bins = Histogram::bins({1.0, qQNaN(), 3.0});
    EXPECT_EQ(bins.total, 2);
}

TEST(Histogram, EmptyInputGivesNoBins)
{
    EXPECT_TRUE(Histogram::bins({}).counts.isEmpty());
    EXPECT_TRUE(Histogram::bins({qQNaN()}).counts.isEmpty());
}

TEST(Histogram, FitNormalRecoversMeanAndSigma)
{
    // Зерно фиксировано: тест обязан давать один и тот же ответ на всех машинах.
    std::mt19937 generator(20260816);
    std::normal_distribution<double> distribution(5.0, 2.0);

    QList<double> values;
    values.reserve(20000);
    for (int i = 0; i < 20000; ++i)
        values.append(distribution(generator));

    const auto normal = Histogram::fitNormal(values);

    ASSERT_TRUE(normal.valid);
    EXPECT_NEAR(normal.mean, 5.0, 0.1);
    EXPECT_NEAR(normal.sigma, 2.0, 0.1);
}

TEST(Histogram, FitNormalNeedsTwoValues)
{
    EXPECT_FALSE(Histogram::fitNormal({1.0}).valid);
}

TEST(Spectrum, SineGivesOnePeakAtItsFrequency)
{
    // 1024 отсчёта на 1 кГц, тон 50 Гц: корзина 0.977 Гц, пик обязан лечь в 51-ю ±1.
    const auto result = Spectrum::compute(sine(1024, 1000.0, 50.0), 1000.0);

    ASSERT_TRUE(result.isValid());
    const int peak = peakBin(result.magnitude);
    EXPECT_NEAR(double(peak) * result.binHz, 50.0, result.binHz * 1.5);
}

TEST(Spectrum, WindowChoiceDoesNotMoveThePeak)
{
    const auto hann = Spectrum::compute(sine(1024, 1000.0, 125.0), 1000.0,
                                        Spectrum::Window::Hann);
    const auto rectangular = Spectrum::compute(sine(1024, 1000.0, 125.0), 1000.0,
                                               Spectrum::Window::Rectangular);

    ASSERT_TRUE(hann.isValid());
    ASSERT_TRUE(rectangular.isValid());
    EXPECT_NEAR(peakBin(hann.magnitude), peakBin(rectangular.magnitude), 1);
}

TEST(Spectrum, WindowGainKeepsAmplitudeComparable)
{
    // Без поправки на когерентное усиление окна высота пика падала бы вдвое, и сравнить
    // два спектра с разными окнами было бы нельзя.
    const auto hann = Spectrum::compute(sine(4096, 1000.0, 125.0, 2.0), 1000.0,
                                        Spectrum::Window::Hann);
    const auto rectangular = Spectrum::compute(sine(4096, 1000.0, 125.0, 2.0), 1000.0,
                                               Spectrum::Window::Rectangular);

    const double hannPeak = hann.magnitude.at(peakBin(hann.magnitude));
    const double flatPeak = rectangular.magnitude.at(peakBin(rectangular.magnitude));

    EXPECT_NEAR(hannPeak, flatPeak, flatPeak * 0.1);
}

TEST(Spectrum, ConstantSignalHasNoPeakAwayFromZero)
{
    // Постоянная составляющая снимается до окна: иначе она размазалась бы по нижним
    // корзинам и любой спектр выглядел бы стеной у нуля.
    QList<double> flat(1024, 5.0);
    const auto result = Spectrum::compute(flat, 1000.0);

    ASSERT_TRUE(result.isValid());
    for (const double value : result.magnitude)
        EXPECT_LT(value, 1e-6);
}

TEST(Spectrum, BinCountIsThePowerOfTwoBelowTheSampleCount)
{
    EXPECT_EQ(Spectrum::sizeFor(1000), 512);
    EXPECT_EQ(Spectrum::sizeFor(1024), 1024);
    EXPECT_EQ(Spectrum::sizeFor(3), 2);
    EXPECT_EQ(Spectrum::sizeFor(1), 0);
}

TEST(Spectrum, BinCountIsCapped)
{
    // Дальше экран всё равно не разрешает, а цена растёт.
    EXPECT_EQ(Spectrum::sizeFor(1'000'000), Spectrum::kMaximumSize);
}

TEST(Spectrum, TooFewSamplesReportsWhy)
{
    const auto result = Spectrum::compute({1.0}, 1000.0);
    EXPECT_FALSE(result.isValid());
    EXPECT_FALSE(result.problem.isEmpty());
}

TEST(Spectrum, ModestJitterIsToleratedWithoutResampling)
{
    // Планировщик всегда даёт небольшой разброс интервалов. Пересэмплировать из-за него
    // значило бы никогда не идти быстрым путём.
    QList<double> values = sine(512, 1000.0, 100.0);
    QList<qint64> stamps;
    qint64 now = 0;
    for (int i = 0; i < values.size(); ++i) {
        now += (i % 2 == 0) ? kSecond / 1000 : kSecond / 900;
        stamps.append(now);
    }

    const auto result = Spectrum::computeFromSamples(values, stamps);

    ASSERT_TRUE(result.isValid());
    EXPECT_FALSE(result.resampled);
}

TEST(Spectrum, EvenlySpacedSamplesAreNotResampled)
{
    QList<double> values = sine(512, 1000.0, 100.0);
    QList<qint64> stamps;
    stamps.reserve(values.size());
    for (int i = 0; i < values.size(); ++i)
        stamps.append(qint64(i) * kSecond / 1000);

    const auto result = Spectrum::computeFromSamples(values, stamps);

    ASSERT_TRUE(result.isValid());
    EXPECT_FALSE(result.resampled);
}

TEST(Spectrum, UnevenSpacingIsResampledAndFlagged)
{
    // Молча пересэмплировать и выдать уверенную ось частот значило бы соврать, поэтому
    // признак поднимается и показывается пользователю.
    // Разброс вдвое — это уже не дрожание планировщика: допуск в полтора раза такое
    // намеренно не прощает.
    QList<double> values;
    QList<qint64> stamps;
    qint64 now = 0;
    for (int i = 0; i < 512; ++i) {
        values.append(std::sin(double(i) / 5.0));
        now += (i % 2 == 0) ? kSecond / 1000 : kSecond / 500;
        stamps.append(now);
    }

    const auto result = Spectrum::computeFromSamples(values, stamps);

    ASSERT_TRUE(result.isValid());
    EXPECT_TRUE(result.resampled);
}

TEST(Spectrum, LargeGapInvalidatesWithAReason)
{
    // Интерполировать через настоящую паузу — значит выдумать данные и получить частоту,
    // взявшуюся из ниоткуда.
    QList<double> values;
    QList<qint64> stamps;
    for (int i = 0; i < 64; ++i) {
        values.append(double(i % 8));
        stamps.append(qint64(i) * kSecond / 1000);
    }
    values.append(1.0);
    stamps.append(stamps.last() + 60 * kSecond);

    const auto result = Spectrum::computeFromSamples(values, stamps);

    EXPECT_FALSE(result.isValid());
    EXPECT_FALSE(result.problem.isEmpty());
}
