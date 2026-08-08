/**
 * \file CsvSeries.h
 * \brief Накопитель числовых рядов, разобранных из CSV.
 */
#pragma once

#include <QList>
#include <QObject>
#include <QString>

namespace spotty {

/**
 * \class CsvSeries
 * \brief Разбирает строки вида `12.5,3,-7` и хранит последние значения каждой колонки.
 *
 * \par Почему модель отдельно от обеих панелей
 *
 * Плагин показывает две вещи: настройки в боковой рейке и сам график поверх вывода. Обе
 * смотрят на одни данные, и ни одна не может владеть ими — панель в рейке пользователь
 * закрывает, а график должен продолжать рисоваться. Владеет моделью плагин.
 */
class CsvSeries : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    /**
     * \brief Разобрать строку вывода.
     * \return `true`, если строка дала хоть одно число.
     *
     * Нечисловые строки молча пропускаются: устройство вперемешку с данными шлёт
     * сообщения о состоянии, и считать их за ошибку значило бы засорять журнал.
     */
    bool feed(const QString &line);

    void clear();

    /// \brief Число рядов; равно числу колонок в самой широкой встреченной строке.
    int seriesCount() const { return int(m_series.size()); }

    const QList<double> &values(int series) const { return m_series.at(series); }

    /// \brief Наименьшее и наибольшее значение по всем рядам; при пустых данных — 0 и 1.
    void range(double *minimum, double *maximum) const;

    /// \brief Сколько последних точек хранить.
    void setCapacity(int points);
    int capacity() const { return m_capacity; }

    /// \brief Разделитель колонок.
    void setSeparator(QChar separator) { m_separator = separator; }

Q_SIGNALS:
    /// \brief Данные изменились — перерисовать график.
    void changed();

private:
    QList<QList<double>> m_series;
    int m_capacity = 200;
    QChar m_separator = u',';
};

} // namespace spotty
