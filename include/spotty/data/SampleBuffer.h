/**
 * \file SampleBuffer.h
 * \brief Кольцевое хранилище отсчётов телеметрии: метка времени плюс строка значений.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>

#include <QList>
#include <QString>
#include <QStringList>

namespace spotty {

/**
 * \class SampleBuffer
 * \brief Последние отсчёты потока: одна метка времени и ровно `columnCount()` значений на
 *        каждый.
 *
 * \par Инвариант
 *
 * После любого публичного вызова:
 * - каждая хранимая строка держит ровно `columnCount()` значений и ровно одну метку времени;
 * - `sampleCount()` — счётчик **всего**, счётчика на отдельную колонку не существует;
 * - каждая ячейка — конечное число либо NaN, где NaN означает «поля в строке не было»;
 * - метки времени не убывают.
 *
 * \par Почему хранение по отсчётам, а не по колонкам
 *
 * Прежняя модель держала N независимых списков значений и отдельный список меток времени,
 * дополняя их вместе «обычно, но не всегда». Строка, пришедшая с меньшим числом полей,
 * не удлиняла хвостовые ряды, и те отставали навсегда; рисовался же каждый ряд растянутым
 * на всю ширину по своей длине, отчего два ряда ложились на график в разных временных
 * масштабах друг поверх друга, а курсор подписывал значение одной колонки именем другой.
 *
 * Причина не в отдельных ошибках, а в том, что раскладка позволяла длинам разойтись.
 * Здесь она этого не позволяет: единица хранения — отсчёт, ширина у него одна на всех, и
 * «у одной колонки больше значений, чем у другой» — состояние, которое просто нельзя
 * выразить. Недостающее поле хранится явным NaN на своём месте, а не пропуском, сдвигающим
 * хвост строки.
 *
 * \par Кольцо, а не подрезка спереди
 *
 * При двухстах точках `removeFirst()` был бесплатен. При пятидесяти тысячах отсчётов на
 * шестнадцать колонок это перекладывание шести мегабайт **на каждую пришедшую строку**.
 * Кольцо делает добавление постоянным по времени независимо от ёмкости.
 */
class SPOTTY_API_EXPORT SampleBuffer
{
public:
    /// \brief Ёмкость по умолчанию, отсчётов.
    static constexpr int kDefaultCapacity = 50000;

    /**
     * \brief Предел числа колонок.
     *
     * Строка шире отвергается целиком, а не обрезается: устройство, вдруг приславшее
     * тысячу полей, потеряло кадровую синхронизацию, и обрезка молча выдумала бы данные.
     */
    static constexpr int kDefaultColumnLimit = 64;

    /**
     * \struct ColumnStats
     * \brief Сводка по колонке. NaN в подсчёте не участвует.
     */
    struct ColumnStats
    {
        double minimum = 0.0;
        double maximum = 0.0;
        double mean = 0.0;
        int finiteCount = 0; ///< Ноль означает «показывать нечего», а не «значение равно нулю».
    };

    SampleBuffer();

    /// \brief Ёмкость в отсчётах; не меньше двух. Ужатие роняет самые старые строки.
    void setCapacity(int samples);
    int capacity() const { return m_capacity; }

    void setColumnLimit(int columns);
    int columnLimit() const { return m_columnLimit; }

    int columnCount() const { return m_stride; }
    int sampleCount() const { return m_size; }

    /// \brief Метка времени строки; строка 0 — самая старая из хранимых.
    qint64 timestamp(int row) const;

    /// \brief Значение; NaN означает, что поля в этой строке не было.
    double at(int row, int column) const;

    /// \brief Значения одной колонки подряд — для БПФ и гистограммы.
    void copyColumn(int column, int fromRow, int count, double *out) const;

    /**
     * \brief Добавить отсчёт.
     * \param monotonicNs Метка времени; зажимается снизу предыдущей, см. \ref timestamp.
     * \param values Значения строки; недостающие до `columnCount()` дополняются NaN.
     * \param count Число значений в \p values.
     * \return `false`, если \p count больше columnLimit() либо не положителен; отсчёт не сохранён.
     *
     * Если \p count больше нынешней ширины, ширина растёт, а прошлые отсчёты добиваются
     * NaN назад — колонка, появившаяся посреди потока, не получает собственной длины.
     */
    bool append(qint64 monotonicNs, const double *values, int count);

    /// \brief Уронить накопленное, сохранив число колонок.
    void clearSamples();

    /// \brief Залить одну колонку NaN, не трогая строки и остальные колонки.
    void clearColumn(int column);

    /// \brief Уронить и отсчёты, и колонки.
    void reset();

    /// \brief Сводка по всему накопленному окну; результат кэшируется.
    ColumnStats stats(int column) const;

    /// \brief Сводка по отрезку строк; не кэшируется.
    ColumnStats stats(int column, int fromRow, int count) const;

    /**
     * \brief Выгрузить накопленное таблицей CSV с заголовком.
     * \param names Имена колонок; недостающие заменяются номером.
     *
     * Отсутствующее значение выходит пустым полем — так его и понимает всякая программа,
     * читающая CSV. Время идёт в миллисекундах от первого хранимого отсчёта.
     */
    QString toCsv(const QStringList &names) const;

private:
    /// \brief Место строки в кольце по её логическому номеру.
    int physicalRow(int row) const;

    /// \brief Расширить строку до \p columns колонок, добив прошлые отсчёты NaN.
    void growStride(int columns);

    /// \brief Переложить кольцо в новые массивы, оставив последние \p keep строк.
    void relayout(int newCapacity, int newStride, int keep);

    void invalidateStats();

    QList<double> m_cells;  ///< Кольцо на `m_capacity` строк по `m_stride` значений.
    QList<qint64> m_stamps; ///< Кольцо меток времени, тех же строк.

    int m_capacity = kDefaultCapacity;
    int m_columnLimit = kDefaultColumnLimit;
    int m_head = 0;   ///< Место самой старой строки в кольце.
    int m_size = 0;   ///< Сколько строк хранится.
    int m_stride = 0; ///< Ширина строки; она же columnCount().

    /**
     * \brief Кэш сводок по колонкам.
     *
     * Считается лениво и сбрасывается добавлением. Инкрементальный пересчёт (монотонные
     * очереди на минимум и максимум) здесь не нужен: таблица обновляется пять раз в
     * секунду, и полный проход по окну обходится дешевле, чем поддержание очередей на
     * каждой пришедшей строке. Не «оптимизировать» без замера.
     */
    mutable QList<ColumnStats> m_statsCache;
    mutable QList<bool> m_statsValid;
};

} // namespace spotty
