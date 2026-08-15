/**
 * \file PlotViewState.h
 * \brief Состояние вида плоттера: окно по времени, масштаб, выделение, режим.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>

#include <QList>
#include <QObject>

namespace spotty {

/**
 * \class PlotViewState
 * \brief Всё, что задаёт вид, но не является данными.
 *
 * \par Одно состояние на все виды
 *
 * Плоттер живёт в трёх местах сразу: миниатюрой в боковой панели, полосой вместо терминала
 * и отдельным окном. Состояние у них общее — это и означает «единый объект»: сдвинув окно
 * в одном виде, пользователь видит то же самое во всех. Хранится оно в единицах данных, а
 * не в пикселях, поэтому три вида разной ширины показывают одно и то же.
 */
class SPOTTY_API_EXPORT PlotViewState : public QObject
{
    Q_OBJECT

public:
    /// \brief Что и как показывает поле графика.
    enum class Mode {
        TimeSeries, ///< Значения от времени.
        Xy,         ///< Один ряд от другого — фазовый портрет.
        Histogram,  ///< Распределение значений с кривой нормального.
        Cumulative, ///< Бегущая сумма.
        Spectrum,   ///< Амплитуда от частоты.
        MultiPlot,  ///< По мини-графику на ряд, общая ось X.
    };
    Q_ENUM(Mode)

    /// \brief Длительность окна по умолчанию, нс. Десять секунд телеметрии.
    static constexpr qint64 kDefaultDuration = 10'000'000'000LL;

    explicit PlotViewState(QObject *parent = nullptr);

    /// \name Окно по времени
    /// @{
    qint64 windowFrom() const { return m_from; }
    qint64 windowTo() const { return m_to; }
    qint64 windowDuration() const { return m_to - m_from; }
    void setWindow(qint64 from, qint64 to);
    void setWindowDuration(qint64 nanoseconds);
    /// @}

    /**
     * \brief Следовать за свежими данными.
     *
     * Ровно как «Follow output» в терминале: ручная прокрутка слежение снимает, а доводка
     * до правого края — возвращает. Один привычный жест на весь продукт.
     */
    bool following() const { return m_following; }
    void setFollowing(bool following);

    /// \brief Сдвинуть окно на \p nanoseconds; слежение при этом снимается.
    void panBy(qint64 nanoseconds);

    /**
     * \brief Изменить масштаб по X вокруг точки \p anchor.
     * \param factor Меньше единицы — приблизить, больше — отдалить.
     *
     * Точка под курсором остаётся на месте: иначе приближение уводит из-под указателя
     * ровно то место, ради которого его и делают.
     */
    void zoomX(double factor, qint64 anchor);

    /// \name Вертикальный масштаб и сдвиг, общие для всех рядов
    /// Ряды меряют разное, поэтому у каждого своя шкала; эти два числа двигают их все
    /// разом, сохраняя относительный вид.
    /// @{
    double verticalZoom() const { return m_verticalZoom; }
    void zoomY(double factor);
    double verticalOffset() const { return m_verticalOffset; }
    void panY(double fraction);
    void resetVertical();
    /// @}

    /**
     * \brief Прижать окно к границам накопленного и подхватить слежение.
     * \param first Время самого старого отсчёта.
     * \param last Время самого свежего.
     *
     * Зовётся перед отрисовкой. Пока идёт слежение, окно едет за \p last, сохраняя
     * длительность; иначе лишь не даёт уехать за пределы буфера, где смотреть нечего.
     */
    void clampTo(qint64 first, qint64 last);

    /**
     * \brief Активный ряд: его шкала подписана слева, его кривая ярче.
     *
     * Влияет только на аннотацию, не на отображение значений. Пока выделена группа из
     * двух и более рядов, активный обязан быть её членом — иначе слева была бы подписана
     * шкала, по которой не нарисован ни один ряд группы.
     */
    int activeSeries() const { return m_active; }
    void setActiveSeries(int index);

    /**
     * \brief Выделенные ряды; при двух и более у них общая шкала.
     *
     * Отдельный механизм от активного ряда: тот отвечает за подписи, этот — за то, как
     * значения ложатся на поле.
     */
    QList<int> selectionGroup() const { return m_selection; }
    void setSelectionGroup(const QList<int> &series);
    bool hasScaleGroup() const { return m_selection.size() >= 2; }
    bool isInScaleGroup(int index) const;

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    bool paused() const { return m_paused; }
    void setPaused(bool paused);

Q_SIGNALS:
    /// \brief Что-либо в виде изменилось — всем трём видам пора перерисоваться.
    void changed();

    /// \brief Слежение включилось или выключилось — кнопка обязана это отразить.
    void followingChanged(bool following);

    void pausedChanged(bool paused);

private:
    qint64 m_from = 0;
    qint64 m_to = kDefaultDuration;
    bool m_following = true;

    double m_verticalZoom = 1.0;
    double m_verticalOffset = 0.0;

    int m_active = -1;
    QList<int> m_selection;

    Mode m_mode = Mode::TimeSeries;
    bool m_paused = false;
};

} // namespace spotty
