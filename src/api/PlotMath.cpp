/**
 * \file PlotMath.cpp
 * \brief Реализация spotty::Histogram и spotty::Spectrum.
 */
#include <spotty/data/PlotMath.h>

#include <QtGlobal>
#include <QtMath>

#include <algorithm>

namespace spotty {

namespace {

/// \brief Ниже этого числа значений межквартильный размах ненадёжен.
constexpr int kSturgesThreshold = 30;

/// \name Пределы числа корзин гистограммы
/// @{
constexpr int kMinimumBins = 1;
constexpr int kMaximumBins = 200;
/// @}

/**
 * \brief Во сколько раз наибольший интервал может превышать наименьший.
 *
 * За этой границей поток считается неравномерным, и перед БПФ его приходится класть на
 * равномерную сетку. Полтора — запас на дрожание планировщика, но не на настоящие паузы.
 */
constexpr double kUniformTolerance = 1.5;

/// \brief Разрыв длиннее стольких медианных интервалов делает спектр недействительным.
constexpr double kMaximumGapIntervals = 3.0;

/// \brief Оставить только конечные значения.
QList<double> finiteOnly(const QList<double> &values)
{
    QList<double> result;
    result.reserve(values.size());
    for (const double value : values) {
        if (qIsFinite(value))
            result.append(value);
    }
    return result;
}

/// \brief Значение квантиля по отсортированному списку, линейной интерполяцией.
double quantile(const QList<double> &sorted, double fraction)
{
    if (sorted.isEmpty())
        return 0.0;
    const double position = fraction * double(sorted.size() - 1);
    const int low = int(position);
    const int high = qMin(low + 1, int(sorted.size() - 1));
    return sorted.at(low) + (position - low) * (sorted.at(high) - sorted.at(low));
}

/// \brief Коэффициент оконной функции для отсчёта \p index из \p size.
double windowFactor(Spectrum::Window window, int index, int size)
{
    if (window == Spectrum::Window::Rectangular || size < 2)
        return 1.0;
    // Ханна: 0.5 - 0.5*cos(2*pi*n/(N-1)).
    return 0.5 - 0.5 * std::cos(2.0 * M_PI * double(index) / double(size - 1));
}

/**
 * \brief Быстрое преобразование Фурье на месте, основание два.
 *
 * Итеративный Кули–Тьюки: около шестидесяти строк и никакой зависимости. Для восьми тысяч
 * точек это доли миллисекунды, а считается спектр не чаще, чем перерисовывается кадр.
 */
void transform(QList<double> &real, QList<double> &imaginary)
{
    const int size = int(real.size());
    if (size < 2)
        return;

    // Перестановка по обращению битов.
    for (int i = 1, j = 0; i < size; ++i) {
        int bit = size >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imaginary[i], imaginary[j]);
        }
    }

    for (int length = 2; length <= size; length <<= 1) {
        const double angle = -2.0 * M_PI / double(length);
        const double stepReal = std::cos(angle);
        const double stepImaginary = std::sin(angle);

        for (int start = 0; start < size; start += length) {
            double factorReal = 1.0;
            double factorImaginary = 0.0;

            for (int k = 0; k < length / 2; ++k) {
                const int even = start + k;
                const int odd = even + length / 2;

                const double oddReal = real[odd] * factorReal - imaginary[odd] * factorImaginary;
                const double oddImaginary =
                    real[odd] * factorImaginary + imaginary[odd] * factorReal;

                real[odd] = real[even] - oddReal;
                imaginary[odd] = imaginary[even] - oddImaginary;
                real[even] += oddReal;
                imaginary[even] += oddImaginary;

                const double nextReal = factorReal * stepReal - factorImaginary * stepImaginary;
                factorImaginary = factorReal * stepImaginary + factorImaginary * stepReal;
                factorReal = nextReal;
            }
        }
    }
}

} // namespace

double Histogram::Bins::width() const
{
    if (counts.isEmpty())
        return 0.0;
    return (maximum - minimum) / double(counts.size());
}

Histogram::Bins Histogram::bins(const QList<double> &values, int binCount)
{
    Bins result;

    QList<double> sorted = finiteOnly(values);
    if (sorted.isEmpty())
        return result;

    std::sort(sorted.begin(), sorted.end());
    result.minimum = sorted.first();
    result.maximum = sorted.last();
    result.total = int(sorted.size());

    if (qFuzzyCompare(result.minimum, result.maximum)) {
        // Все значения одинаковы: одна корзина — честный ответ, а деление на нулевую
        // ширину дало бы бесконечность.
        result.counts = QList<int>{int(sorted.size())};
        result.maximum = result.minimum + 1.0;
        return result;
    }

    int wanted = binCount;
    if (wanted <= 0) {
        if (sorted.size() < kSturgesThreshold) {
            // Стёрджес: на короткой выборке межквартильный размах ненадёжен.
            wanted = int(std::ceil(std::log2(double(sorted.size())) + 1.0));
        } else {
            // Фридман–Дьяконис: ширина от межквартильного размаха, поэтому одиночный
            // выброс не сминает всю картинку в одну корзину.
            const double iqr = quantile(sorted, 0.75) - quantile(sorted, 0.25);
            const double width = 2.0 * iqr / std::cbrt(double(sorted.size()));
            wanted = width > 0.0 ? int(std::ceil((result.maximum - result.minimum) / width))
                                 : int(std::ceil(std::sqrt(double(sorted.size()))));
        }
    }

    wanted = qBound(kMinimumBins, wanted, kMaximumBins);
    result.counts.fill(0, wanted);

    const double width = (result.maximum - result.minimum) / double(wanted);
    for (const double value : sorted) {
        // Верхняя граница попадает в последнюю корзину, а не в несуществующую следующую.
        const int index = qBound(0, int((value - result.minimum) / width), wanted - 1);
        ++result.counts[index];
    }

    return result;
}

Histogram::Normal Histogram::fitNormal(const QList<double> &values)
{
    Normal result;

    const QList<double> finite = finiteOnly(values);
    if (finite.size() < 2)
        return result;

    double sum = 0.0;
    for (const double value : finite)
        sum += value;
    result.mean = sum / double(finite.size());

    double squares = 0.0;
    for (const double value : finite) {
        const double delta = value - result.mean;
        squares += delta * delta;
    }
    // Деление на N−1: значения — выборка из потока, а не вся совокупность.
    result.sigma = std::sqrt(squares / double(finite.size() - 1));
    result.valid = true;
    return result;
}

double Histogram::normalDensity(const Normal &normal, double value)
{
    if (!normal.valid || normal.sigma <= 0.0)
        return 0.0;

    const double z = (value - normal.mean) / normal.sigma;
    return std::exp(-0.5 * z * z) / (normal.sigma * std::sqrt(2.0 * M_PI));
}

int Spectrum::sizeFor(int count)
{
    int size = 1;
    while (size * 2 <= count && size * 2 <= kMaximumSize)
        size *= 2;
    return size >= 2 ? size : 0;
}

Spectrum::Result Spectrum::compute(const QList<double> &uniform, double sampleRateHz,
                                   Window window)
{
    Result result;

    const int size = sizeFor(int(uniform.size()));
    if (size < 2 || sampleRateHz <= 0.0) {
        result.problem = QStringLiteral("not enough samples");
        return result;
    }

    // Берётся хвост: свежие данные интереснее старых, и именно они на экране.
    const int offset = int(uniform.size()) - size;

    double mean = 0.0;
    for (int i = 0; i < size; ++i)
        mean += uniform.at(offset + i);
    mean /= double(size);

    QList<double> real;
    QList<double> imaginary;
    real.reserve(size);
    imaginary.fill(0.0, size);

    double windowSum = 0.0;
    for (int i = 0; i < size; ++i) {
        const double factor = windowFactor(window, i, size);
        windowSum += factor;
        // Постоянная составляющая снимается **до** окна: иначе боковые лепестки размажут
        // большой ноль по нижним корзинам, и любой спектр будет выглядеть стеной у нуля.
        real.append((uniform.at(offset + i) - mean) * factor);
    }

    transform(real, imaginary);

    // Когерентное усиление окна: без него смена окна меняла бы высоту пиков, и сравнивать
    // два спектра было бы нельзя.
    const double gain = windowSum / double(size);
    const double scale = 2.0 / (double(size) * (gain > 0.0 ? gain : 1.0));

    result.magnitude.reserve(size / 2);
    for (int i = 0; i < size / 2; ++i) {
        result.magnitude.append(
            std::hypot(real.at(i), imaginary.at(i)) * scale);
    }

    result.binHz = sampleRateHz / double(size);
    result.usedSamples = size;
    return result;
}

Spectrum::Result Spectrum::computeFromSamples(const QList<double> &values,
                                              const QList<qint64> &timestamps, Window window)
{
    Result result;

    if (values.size() != timestamps.size() || values.size() < 4) {
        result.problem = QStringLiteral("not enough samples");
        return result;
    }

    QList<qint64> intervals;
    intervals.reserve(timestamps.size() - 1);
    for (int i = 1; i < timestamps.size(); ++i) {
        const qint64 delta = timestamps.at(i) - timestamps.at(i - 1);
        if (delta > 0)
            intervals.append(delta);
    }
    if (intervals.isEmpty()) {
        result.problem = QStringLiteral("timestamps do not advance");
        return result;
    }

    std::sort(intervals.begin(), intervals.end());
    const qint64 median = intervals.at(intervals.size() / 2);
    const qint64 shortest = intervals.first();
    const qint64 longest = intervals.last();

    if (double(longest) > kMaximumGapIntervals * double(median)) {
        // Настоящий разрыв, а не дрожание: интерполировать через него — значит выдумать
        // данные, которых не было, и получить частоту, взявшуюся из ниоткуда.
        result.problem = QStringLiteral("gap in the data");
        return result;
    }

    const double rate = 1e9 / double(median);
    if (double(longest) <= kUniformTolerance * double(shortest)) {
        // Пропуск нельзя пронести в БПФ: NaN отравит всю свёртку. Сравнивать через
        // QList::contains() бесполезно — NaN не равен сам себе, — поэтому проход явный.
        bool clean = true;
        for (const double value : values) {
            if (!qIsFinite(value)) {
                clean = false;
                break;
            }
        }
        if (clean)
            return compute(values, rate, window);
    }

    // Равномерная сетка по медианному интервалу, линейной интерполяцией.
    const qint64 from = timestamps.first();
    const qint64 to = timestamps.last();
    const int count = int((to - from) / median) + 1;
    if (count < 4) {
        result.problem = QStringLiteral("not enough samples");
        return result;
    }

    QList<double> grid;
    grid.reserve(count);
    int cursor = 0;
    for (int i = 0; i < count; ++i) {
        const qint64 when = from + qint64(i) * median;
        while (cursor + 1 < timestamps.size() && timestamps.at(cursor + 1) < when)
            ++cursor;

        const int next = qMin(cursor + 1, int(timestamps.size()) - 1);
        const double a = values.at(cursor);
        const double b = values.at(next);
        if (!qIsFinite(a) || !qIsFinite(b)) {
            grid.append(qIsFinite(a) ? a : (qIsFinite(b) ? b : 0.0));
            continue;
        }

        const qint64 span = timestamps.at(next) - timestamps.at(cursor);
        const double fraction = span > 0 ? double(when - timestamps.at(cursor)) / double(span)
                                         : 0.0;
        grid.append(a + (b - a) * qBound(0.0, fraction, 1.0));
    }

    Result resampled = compute(grid, rate, window);
    resampled.resampled = true;
    return resampled;
}

} // namespace spotty
