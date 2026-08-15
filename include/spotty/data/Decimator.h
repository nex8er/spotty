/**
 * \file Decimator.h
 * \brief Сведение ряда отсчётов к разрешению поля графика.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>
#include <spotty/data/PlotTransform.h>
#include <spotty/data/SampleBuffer.h>

#include <QList>

namespace spotty {

/**
 * \class Decimator
 * \brief Пара точек на пиксельную колонку: наибольшее и наименьшее из попавших в неё.
 *
 * \par Почему в единицах данных, а не сразу в пикселях
 *
 * Видимые пределы ряда выпадают из того же прохода, который и так уже идёт, поэтому
 * автомасштаб по ряду достаётся бесплатно: отдельного прохода нет, и шкала не отстаёт на
 * кадр. Перевод в пиксели остаётся за виджетом, где известна геометрия поля — и это же
 * позволяет проверять всё здешнее без QApplication.
 *
 * \par Почему корзины по времени, а не по номеру отсчёта
 *
 * Раньше ряд растягивался на всю ширину по числу своих точек. Пачка данных, пришедшая за
 * миллисекунду, занимала столько же места, сколько минута тишины, а два ряда разной длины
 * ложились в разных масштабах друг поверх друга. Корзина здесь — отрезок времени, поэтому
 * пачка занимает свою долю ширины, а ряды заведомо совпадают по горизонтали.
 */
class SPOTTY_API_EXPORT Decimator
{
public:
    /// \brief Что накапливать по ходу прохода.
    enum class Accumulator {
        None,       ///< Значения как есть.
        RunningSum, ///< Бегущая сумма от начала буфера — режим накопления.
    };

    /**
     * \struct Column
     * \brief Пиксельная колонка: её номер и пределы попавших в неё значений.
     */
    struct Column
    {
        int x = 0;
        double minimum = 0.0;
        double maximum = 0.0;
    };

    /**
     * \struct Result
     * \brief Свёрнутый ряд вместе с его видимыми пределами.
     */
    struct Result
    {
        QList<Column> columns;

        /**
         * \brief Индексы в #columns, с которых начинается новый отрезок кривой.
         *
         * Пропуск (NaN) рвёт кривую: соединив соседей прямой, мы показали бы данные,
         * которых устройство не присылало. Первый отрезок начинается с нуля, поэтому список
         * непуст всегда, когда есть хоть одна колонка.
         */
        QList<int> runStarts;

        PlotScales::Range visible;
    };

    /**
     * \brief Свести колонку буфера к \p pixelColumns колонкам.
     * \param buffer Источник отсчётов.
     * \param column Номер колонки в буфере.
     * \param transform Окно по времени; поля left и width не используются.
     * \param pixelColumns Сколько колонок в поле графика; не больше — разрешения нет.
     * \param accumulator Что накапливать по ходу.
     */
    static Result reduce(const SampleBuffer &buffer, int column, const XTransform &transform,
                         int pixelColumns, Accumulator accumulator = Accumulator::None);

    /**
     * \brief Номер первой строки со временем не меньше \p time.
     *
     * Двоичный поиск по неубывающим меткам: именно он делает масштабирование по
     * пятидесяти тысячам отсчётов дешёвым — окно находится за логарифм, а не за проход.
     */
    static int lowerBound(const SampleBuffer &buffer, qint64 time);
};

} // namespace spotty
