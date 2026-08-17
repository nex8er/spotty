/**
 * \file PlotCanvas.h
 * \brief Поле графика: отрисовка рядов, сетки и перекрестия.
 */
#pragma once

#include <spotty/data/Decimator.h>
#include <spotty/data/PlotTransform.h>

#include <QHash>
#include <QWidget>

#include <array>
#include <optional>

class QTimer;

namespace spotty {

class IPanelHost;
class PlotModel;
class PlotViewState;

/**
 * \class PlotCanvas
 * \brief Рисует накопленные ряды по общему состоянию вида.
 *
 * \par Один объект в трёх местах
 *
 * Холст создаётся трижды: миниатюрой в боковой панели, полосой вместо терминала и в
 * отдельном окне. Данные и состояние вида у всех трёх общие, поэтому это один плоттер,
 * показанный в трёх местах, а не три разных графика.
 *
 * \par Сглаживание выключено безусловно
 *
 * Замер: одна и та же кривая из 200 точек рисуется 2.9 мс гладкой и 676 мс дёрганой — цена
 * зависит не от числа точек, а от длины пути пера. Дешёвого признака «здесь оно по карману»
 * не нашлось, а разброс — тысячекратный.
 *
 * \par Шкала у каждого ряда своя
 *
 * Ряды меряют разные величины. Одна шкала на всех прижимала бы милливольты к нулю рядом с
 * оборотами в минуту, и половина рядов превращалась бы в прямую у края поля.
 */
class PlotCanvas : public QWidget
{
    Q_OBJECT

public:
    PlotCanvas(IPanelHost *host, PlotModel *model, PlotViewState *view,
               QWidget *parent = nullptr);

    /// \brief Сохранить снимок графика в файл PNG.
    bool saveImage(const QString &filePath);

    /// \brief Снимок графика как изображение — для буфера обмена.
    QPixmap snapshot() const;

    /**
     * \brief Объявить действия, которыми управляют графиком.
     *
     * Действия добавляются в сам виджет (QWidget::addAction), и приложение показывает их
     * кнопками в панели управления областью вывода, когда полоса активна. Отдельного
     * метода в IPanelHost для этого не нужно: QWidget::actions() уже есть, и знание о
     * графике остаётся внутри графика.
     */
    void createActions();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

    /**
     * \brief Колесо: прокрутка и масштаб под модификаторами.
     *
     * Без модификаторов — по горизонтали, потому что горизонталь у графика и есть главная
     * ось. Shift — по вертикали, Ctrl — масштаб X, Alt — масштаб Y.
     *
     * \note На macOS Qt отображает в `Qt::ControlModifier` клавишу Cmd, а в `Qt::AltModifier`
     *       — Option. Это то же соответствие, что у Ctrl+колеса в терминале, поэтому жест
     *       остаётся привычным в пределах программы.
     */
    void wheelEvent(QWheelEvent *event) override;

    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    /// \brief Наибольший интервал между перерисовками: 16 мс — это 60 кадров в секунду.
    static constexpr int kRepaintIntervalMs = 16;

    /**
     * \struct SeriesFrame
     * \brief Всё, что нужно знать о ряде в этом кадре.
     */
    struct SeriesFrame
    {
        int index = 0;
        Decimator::Result reduced;
        YScale scale;
    };

    /// \brief Прямоугольник поля графика без подписей осей.
    QRect plotArea() const;

    /// \name Полосы шкал вокруг поля
    /// Колесо над ними масштабирует свою ось без модификаторов: это то место, где на
    /// масштаб и показывают пальцем.
    /// @{
    QRect verticalScaleRect() const;
    QRect horizontalScaleRect() const;
    /// @}

    /// \brief Окно горизонтальной развёртки в координатах поля. Состояния не меняет.
    XTransform transformFor(const QRect &area) const;

    /// \brief Колонка монотонного счётчика для времеподобных режимов; -1 — время приёма.
    int horizontalCoordinateColumn() const;

    /// \brief Координата строки в текущей горизонтальной системе отсчёта.
    std::optional<qint64> horizontalCoordinateAt(int row) const;

    /// \brief Подвинуть окно за пришедшими данными и запросить перерисовку.
    void followNewData();

    /// \brief Свести все видимые ряды и раздать им шкалы.
    QList<SeriesFrame> buildFrames(const QRect &area, const XTransform &transform) const;

    /// \brief Ширина левого поля, достаточная для подписей нынешней вертикальной шкалы.
    int verticalAxisMargin(const QList<SeriesFrame> &frames) const;

    /// \brief Ширина поля, достаточная для подписей одной числовой шкалы.
    int valueAxisMargin(const YScale &scale) const;

    /// \brief Приложить общий вертикальный масштаб и сдвиг к пределам ряда.
    YScale applyVertical(const PlotScales::Range &range, const QRect &area) const;

    /// \brief Ряд, чья шкала подписана слева; -1, если рисовать нечего.
    int labelledSeries(const QList<SeriesFrame> &frames) const;

    /**
     * \brief Вкладка ряда на левом поле — по ней выбирают, чья шкала подписана.
     * \param position Номер ряда среди видимых, сверху вниз.
     * \param count Сколько видимых рядов всего.
     *
     * Узкая цветная полоска у самого края поля: подписи значений занимают остальное левое
     * поле, и класть вкладки поверх них значило бы перекрыть то, ради чего поле и заведено.
     */
    QRect seriesTabRect(int position, int count, const QRect &area) const;

    /// \brief Ряд под точкой на левом поле; -1, если там ничего нет.
    int seriesTabAt(const QPoint &point) const;

    /**
     * \brief Обработать щелчок по вкладке оси с учётом модификаторов.
     *
     * Без модификатора — выбрать одну ось. С Ctrl — добавить или убрать из группы, с
     * Shift — взять всё между нынешней активной и нажатой. Ровно как выделение строк в
     * таблице сбоку: жест один и тот же, и запоминать его отдельно не приходится.
     */
    void selectAxis(int series, Qt::KeyboardModifiers modifiers);

    void drawFrame(QPainter &painter, const QRect &area, const XTransform &transform,
                   const QList<SeriesFrame> &frames) const;
    void drawValueAxis(QPainter &painter, const QRect &area, const YScale &scale,
                       const QColor &labelColor) const;
    void drawHorizontalAxis(QPainter &painter, const QRect &area,
                            const XTransform &transform) const;
    void drawNumericHorizontalAxis(QPainter &painter, const QRect &area, double minimum,
                                   double maximum) const;
    void drawSeries(QPainter &painter, const QRect &area, const XTransform &transform,
                    const QList<SeriesFrame> &frames) const;

    /// \name Режимы показа
    /// Общее — фон, поле, рамка, сетка, курсор и запрет сглаживания — остаётся снаружи;
    /// каждый режим рисует только свои метки внутри поля.
    /// @{

    /// \brief Развёртка по времени; она же основа режима накопления.
    void drawTimeSeries(QPainter &painter, const QRect &area, const XTransform &transform,
                        const QList<SeriesFrame> &frames) const;

    /// \brief График рядов по выбранной колонке X без участия временных меток.
    void drawXy(QPainter &painter, const QRect &area) const;

    /// \brief По мини-графику на ряд, общая ось X, своя шкала у каждого.
    void drawMultiPlot(QPainter &painter, const QRect &area, const XTransform &transform);

    /// \brief Распределение значений активного ряда с кривой нормального и метками μ, ±σ.
    void drawHistogram(QPainter &painter, const QRect &area) const;

    /// \brief Амплитудный спектр активного ряда.
    void drawSpectrum(QPainter &painter, const QRect &area) const;
    /// @}

    /// \brief Ряд, по которому строится одномерный режим: активный либо первый видимый.
    int subjectSeries() const;

    /// \brief Значения активного ряда в видимом окне вместе с метками времени.
    void visibleValues(QList<double> *values, QList<qint64> *timestamps) const;

    /// \brief Кривая одного ряда отрезками; общая для развёртки и мультиплота.
    void drawCurve(QPainter &painter, const QRect &area, const SeriesFrame &frame,
                   const QColor &colour, bool withFill) const;
    void drawCursor(QPainter &painter, const QRect &area, const XTransform &transform,
                    const QList<SeriesFrame> &frames) const;

    /// \brief Пометить картинку устаревшей и завести таймер перерисовки, если он не идёт.
    void scheduleRepaint();

    IPanelHost *m_host = nullptr;
    PlotModel *m_model = nullptr;
    PlotViewState *m_view = nullptr;

    /// \brief Ширина поля подписей Y для текущего режима.
    int m_leftMargin = 56;

    /**
     * \brief Наибольшая уже показанная ширина шкалы для каждого режима.
     *
     * Менять геометрию холста при переходе `9.9 → 10` заметнее самого обновления данных.
     * Запас растёт лишь при необходимости и живёт до смены режима, где своя шкала.
     */
    std::array<int, 6> m_axisMargins{};

    /// \brief Положение курсора в поле графика; -1, когда курсора нет.
    int m_cursorX = -1;

    /**
     * \brief Номера видимых рядов с прошлой отрисовки, сверху вниз.
     *
     * Нужны, чтобы попадание мышью по вкладке оси не требовало заново сводить данные:
     * состав видимых рядов между кадром и щелчком не меняется.
     */
    QList<int> m_visibleOrder;

    /**
     * \enum DragAxis
     * \brief Что двигает начатое перетаскивание.
     *
     * Зависит от того, где нажали: в поле тянутся обе оси разом, за шкалу — только её.
     * Тащить за шкалу удобно, когда нужно поправить одну ось, не сбив вторую.
     */
    enum class DragAxis { Both, Horizontal, Vertical };

    /// \name Перетаскивание поля
    /// @{
    bool m_dragging = false;
    DragAxis m_dragAxis = DragAxis::Both;
    QPoint m_dragOrigin;
    qint64 m_dragFrom = 0;

    /// \brief Вертикальный сдвиг в момент нажатия; тащим от него, а не от прошлого события.
    double m_dragOffset = 0.0;
    /// @}

    /**
     * \brief Непотраченные пиксели прокрутки трекпада.
     *
     * Трекпад сообщает движение в пикселях, а не щелчками колеса. Отбрасывая остаток,
     * плавное движение пальцем шло бы рывками — тот же приём, что в TerminalView.
     */
    QPointF m_wheelRemainder;

    QTimer *m_repaintTimer = nullptr;
    bool m_dirty = false;

    /// \brief Источник X, для которого уже подготовлено начальное окно.
    int m_initializedCoordinateColumn = -1;
};

} // namespace spotty
