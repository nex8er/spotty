/**
 * \file PlotMath.h
 * \brief Гистограмма и спектр: математика режимов плоттера без единого виджета.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>

#include <QList>
#include <QString>

namespace spotty {

/**
 * \class Histogram
 * \brief Распределение значений по корзинам и подгонка нормальной кривой.
 */
class SPOTTY_API_EXPORT Histogram
{
public:
    /**
     * \struct Bins
     * \brief Корзины равной ширины на отрезке [minimum, maximum].
     */
    struct Bins
    {
        QList<int> counts;
        double minimum = 0.0;
        double maximum = 1.0;
        int total = 0; ///< Сколько конечных значений вообще попало в подсчёт.

        double width() const;
    };

    /**
     * \struct Normal
     * \brief Параметры подогнанного нормального распределения.
     */
    struct Normal
    {
        double mean = 0.0;
        double sigma = 0.0;
        bool valid = false;
    };

    /**
     * \brief Разложить значения по корзинам.
     * \param values Значения; NaN пропускаются.
     * \param binCount Число корзин; ноль означает «выбрать самому».
     *
     * Число корзин по умолчанию — правило Фридмана–Дьякониса: ширина корзины выводится из
     * межквартильного размаха, поэтому выброс не растягивает картинку в одну сплошную
     * корзину, как это делает правило по размаху. На выборке меньше тридцати значений
     * межквартильный размах ненадёжен, и берётся правило Стёрджеса.
     */
    static Bins bins(const QList<double> &values, int binCount = 0);

    /**
     * \brief Среднее и выборочное отклонение.
     *
     * Отклонение выборочное, с делением на N−1: значения — это выборка из потока, а не вся
     * генеральная совокупность, и деление на N занижало бы разброс.
     */
    static Normal fitNormal(const QList<double> &values);

    /// \brief Плотность нормального распределения в точке.
    static double normalDensity(const Normal &normal, double value);
};

/**
 * \class Spectrum
 * \brief Амплитудный спектр ряда: БПФ с окном и честной оговоркой о равномерности.
 */
class SPOTTY_API_EXPORT Spectrum
{
public:
    /// \brief Оконная функция.
    enum class Window {
        Rectangular, ///< Без окна: годится только для целого числа периодов.
        Hann,        ///< Умолчание: разумный размен между разрешением и растеканием.
    };

    /// \brief Наибольшее число корзин. Дальше экран всё равно не разрешает.
    static constexpr int kMaximumSize = 8192;

    /**
     * \struct Result
     * \brief Спектр вместе с признаками того, насколько ему можно верить.
     */
    struct Result
    {
        QList<double> magnitude;
        double binHz = 0.0;
        int usedSamples = 0;

        /**
         * \brief Отсчёты пришлось положить на равномерную сетку.
         *
         * Показывается пользователю. Молча пересэмплировать и выдать уверенную ось частот
         * значило бы соврать: строки приходят когда устройству вздумается, и частота,
         * посчитанная по среднему интервалу, к действительности может не иметь отношения.
         */
        bool resampled = false;

        /// \brief Почему считать не удалось; пусто, если удалось.
        QString problem;

        bool isValid() const { return !magnitude.isEmpty(); }
    };

    /**
     * \brief Спектр равномерно взятых значений.
     * \param uniform Значения с постоянным шагом по времени.
     * \param sampleRateHz Частота взятия отсчётов.
     */
    static Result compute(const QList<double> &uniform, double sampleRateHz,
                          Window window = Window::Hann);

    /**
     * \brief Спектр по значениям с метками времени.
     * \param values Значения; NaN означает пропуск.
     * \param timestamps Метки времени, нс; той же длины, что \p values.
     *
     * Если разброс интервалов невелик, отсчёты берутся как есть. Иначе они кладутся на
     * равномерную сетку линейной интерполяцией, и в результате поднимается признак
     * #Result::resampled.
     */
    static Result computeFromSamples(const QList<double> &values,
                                     const QList<qint64> &timestamps,
                                     Window window = Window::Hann);

    /// \brief Наибольшая степень двойки, не превосходящая \p count; не больше kMaximumSize.
    static int sizeFor(int count);
};

} // namespace spotty
