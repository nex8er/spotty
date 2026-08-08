/**
 * \file CsvSeries.h
 * \brief Накопитель числовых рядов, разобранных из CSV.
 */
#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>

namespace spotty {

/**
 * \class CsvSeries
 * \brief Разбирает строки вида `12.5,3,-7` и хранит последние значения каждой колонки.
 *
 * \par Почему модель отдельно от панелей
 *
 * Плагин показывает три вещи: настройки с таблицей рядов, график в области терминала и
 * график в отдельном окне. Все смотрят на одни данные, и ни одна не может ими владеть —
 * панель закрывают, окно закрывают, а накопление должно продолжаться. Владеет плагин.
 */
class CsvSeries : public QObject
{
    Q_OBJECT

public:
    /**
     * \struct Series
     * \brief Один ряд: значения, оформление и подпись.
     */
    struct Series
    {
        QList<double> values;
        QColor color;
        QString name;      ///< Заголовок из первой строки либо «Колонка N».
        bool visible = true;
    };

    using QObject::QObject;

    /**
     * \brief Разобрать строку вывода.
     * \return `true`, если строка дала хоть одно число.
     *
     * Нечисловые строки молча пропускаются: устройства вперемешку с данными шлют
     * сообщения о состоянии, и считать их за ошибку значило бы засорять журнал.
     *
     * \param monotonicNs Отметка времени строки. Нужна для оси X по времени: точки
     *        приходят неравномерно, и равноотстоящая ось искажает форму сигнала.
     */
    bool feed(const QString &line, qint64 monotonicNs);

    void clear();

    int seriesCount() const { return int(m_series.size()); }
    const Series &series(int index) const { return m_series.at(index); }

    void setSeriesVisible(int index, bool visible);
    void setSeriesColor(int index, const QColor &color);

    /**
     * \brief Номер ряда, отложенного по оси X, либо -1 для оси времени.
     *
     * По умолчанию -1: ось времени честнее номера отсчёта, а колонку под X назначают,
     * когда устройство само шлёт координату.
     */
    void setXAxisSeries(int index);
    int xAxisSeries() const { return m_xAxis; }

    /// \brief Отметки времени точек, в наносекундах от начала отсчёта канала.
    const QList<qint64> &timestamps() const { return m_timestamps; }

    /// \brief Наименьшее и наибольшее среди видимых рядов; при пустых данных — 0 и 1.
    void range(double *minimum, double *maximum) const;

    /// \name Статистика ряда по накопленному окну
    /// @{
    double minimumOf(int index) const;
    double maximumOf(int index) const;
    double averageOf(int index) const;
    /// @}

    void setCapacity(int points);
    int capacity() const { return m_capacity; }

    void setSeparator(QChar separator) { m_separator = separator; }

    /// \brief Выгрузить накопленное таблицей CSV с заголовком.
    QString toCsv() const;

Q_SIGNALS:
    void changed();

    /// \brief Изменился состав рядов — таблица должна перестроиться.
    void seriesAdded();

private:
    /// \brief Цвет очередного ряда из встроенной палитры.
    static QColor defaultColor(int index);

    QList<Series> m_series;
    QList<qint64> m_timestamps;
    int m_capacity = 200;
    int m_xAxis = -1;
    QChar m_separator = u',';
};

} // namespace spotty
