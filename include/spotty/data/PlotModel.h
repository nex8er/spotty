/**
 * \file PlotModel.h
 * \brief Данные плоттера: отсчёты, оформление рядов и разбор входящих строк.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>
#include <spotty/data/SampleBuffer.h>
#include <spotty/data/SampleParser.h>

#include <QList>
#include <QObject>
#include <QString>

namespace spotty {

/**
 * \struct PlotSeries
 * \brief Оформление одного ряда. Значений здесь нет — они в SampleBuffer.
 *
 * \note Цвет хранится как `0xAARRGGBB`, а не `QColor`: тот объявлен в QtGui, с которым этот
 *       слой не линкуется. Ровно так же поступают spotty::TextStyle и spotty::HighlightRules.
 *       Распаковкой занимается слой виджетов.
 */
struct PlotSeries
{
    QString name;
    bool nameIsCustom = false; ///< Имя задал пользователь; заголовок устройства его не тронет.
    quint32 color = 0;
    bool visible = true;

    /// \name Пользовательские пределы шкалы
    /// Заданные вручную, они не перебиваются ни автомасштабом, ни общей шкалой группы.
    /// @{
    bool hasCustomRange = false;
    double customMinimum = 0.0;
    double customMaximum = 1.0;
    /// @}
};

/**
 * \class PlotModel
 * \brief Накопитель отсчётов вместе с оформлением рядов.
 *
 * \par Почему оформление лежит отдельно от значений
 *
 * Прежняя модель держала значения, цвет, имя и видимость в одной структуре, и очистка
 * вынужденно роняла всё разом: пользователь просил стереть накопленное, а терял и
 * подобранные цвета, и выбор оси X. Здесь очистка физически не может тронуть оформление —
 * она им не владеет.
 *
 * \par Владелец
 *
 * Модель принадлежит плагину, а не панели: панель закрывают, окно закрывают, а накопление
 * должно продолжаться. На одну модель смотрят все виды сразу.
 */
class SPOTTY_API_EXPORT PlotModel : public QObject
{
    Q_OBJECT

public:
    explicit PlotModel(QObject *parent = nullptr);

    /**
     * \brief Разобрать строку вывода и, если это данные, добавить отсчёт.
     * \param monotonicNs Отметка времени чтения строки.
     * \return `true`, если строка дала отсчёт.
     *
     * Строка-заголовок переименовывает ряды и возвращает `false`: отсчёта она не даёт.
     */
    bool feed(const QString &line, qint64 monotonicNs);

    /// \brief Уронить накопленное. Цвета, имена, видимость и пределы остаются.
    void clearSamples();

    /// \brief Уронить накопленное одной колонки, не трогая остальные.
    void clearColumn(int column);

    const SampleBuffer &samples() const { return m_samples; }

    int seriesCount() const { return int(m_series.size()); }
    const PlotSeries &series(int index) const { return m_series.at(index); }

    void setSeriesVisible(int index, bool visible);
    void setSeriesColor(int index, quint32 color);

    /// \brief Переименовать ряд вручную; имя переживёт заголовок от устройства.
    void setSeriesName(int index, const QString &name);

    /// \brief Задать или снять пользовательские пределы шкалы ряда.
    void setSeriesRange(int index, bool hasRange, double minimum, double maximum);

    /// \brief Имена всех рядов по порядку — для экспорта и для опознания профиля.
    QStringList seriesNames() const;

    /**
     * \brief Номер ряда, отложенного по оси X, либо -1 для оси времени.
     *
     * По умолчанию -1: время честнее номера отсчёта, а колонку под X назначают, когда
     * устройство само шлёт координату.
     */
    void setXAxisSeries(int index);
    int xAxisSeries() const { return m_xAxis; }

    void setCapacity(int samples);
    int capacity() const { return m_samples.capacity(); }

    void setSeparator(QChar separator);
    QChar separator() const { return m_parser.separator(); }

    /// \brief Сколько строк отвергнуто из-за превышения предела колонок.
    int rejectedCount() const { return m_rejected; }

    /// \brief Выгрузить накопленное таблицей CSV с заголовком.
    QString toCsv() const { return m_samples.toCsv(seriesNames()); }

Q_SIGNALS:
    /// \brief Данные или оформление изменились — вид должен перерисоваться.
    void changed();

    /// \brief Изменился состав рядов — таблица должна перестроиться.
    void seriesAdded();

private:
    /// \brief Довести число рядов до ширины отсчёта, раздав имена и цвета.
    bool growSeries(int columns);

    /// \brief Цвет очередного ряда из встроенной палитры.
    static quint32 defaultColor(int index);

    SampleBuffer m_samples;
    SampleParser m_parser;
    QList<PlotSeries> m_series;
    int m_xAxis = -1;
    int m_rejected = 0;
};

} // namespace spotty
