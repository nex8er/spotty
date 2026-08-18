/**
 * \file PlotCanvas.cpp
 * \brief Реализация spotty::PlotCanvas.
 */
#include "PlotCanvas.h"

#include <spotty/data/PlotFormat.h>
#include <spotty/data/PlotMath.h>
#include <spotty/data/PlotModel.h>
#include <spotty/data/PlotViewState.h>

#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>

namespace spotty {

namespace {

/// \brief Поля вокруг поля графика: слева под подписи значений, снизу под ось времени.
constexpr int kMinimumMarginLeft = 56;
constexpr int kMarginRight = 10;
constexpr int kMarginTop = 10;
constexpr int kMarginBottom = 26;

/// \brief Прозрачность заливки под линией.
constexpr int kFillAlpha = 36;

/**
 * \brief До скольких рядов под кривой рисуется заливка.
 *
 * Заливка подчёркивает форму, пока кривых одна-две. Десять полупрозрачных заливок друг на
 * друге дают мутное пятно, сквозь которое не читается ни одна, — и стоят при этом дороже
 * всей остальной отрисовки вместе взятой.
 */
constexpr int kMaxFilledSeries = 3;

/// \brief Число горизонтальных линий сетки, не считая краёв.
constexpr int kGridLines = 3;

/// \brief Прозрачность ряда, не участвующего в подписанной шкале.
constexpr int kDimmedAlpha = 90;

/// \brief Число значащих цифр в подписях шкалы и перекрестия.
constexpr int kLabelDigits = 5;

/// \brief Число значащих цифр у меток шкалы: компактнее значения под курсором.
constexpr int kAxisLabelDigits = 4;

/// \brief Ширина цветной вкладки оси у левого края поля, px.
constexpr int kAxisTabWidth = 5;

/// \brief Отбивка между подписями и шкалой, px.
constexpr int kAxisLabelGap = 5;

/// \brief Минимальный зазор между двумя подписями на горизонтальной оси, px.
constexpr int kHorizontalLabelGap = 8;

/// \brief Длительность в короткой записи для горизонтальной оси и перекрестия.
QString formatTime(qint64 nanoseconds, qint64 reference = 0)
{
    struct Unit {
        qint64 nanoseconds;
        const char *suffix;
    };
    constexpr Unit kUnits[] = {
        {60'000'000'000LL, " min"},
        {1'000'000'000LL, " s"},
        {1'000'000LL, " ms"},
        {1'000LL, " us"},
        {1LL, " ns"},
    };

    const qint64 absolute = qMax(qAbs(nanoseconds), qAbs(reference));
    for (const Unit &unit : kUnits) {
        if (absolute < unit.nanoseconds && unit.nanoseconds != 1)
            continue;
        return PlotFormat::number(double(nanoseconds) / double(unit.nanoseconds),
                                  kAxisLabelDigits)
               + QLatin1String(unit.suffix);
    }
    return {};
}

/**
 * \brief Оставить самый длинный непрерывный участок для БПФ.
 *
 * БПФ нельзя строить через паузу приёма: интерполяция добавит несуществующую низкую
 * частоту. Отказ целиком, однако, лишает пользователя всех данных до или после единичной
 * остановки потока, поэтому выбирается самый длинный честный фрагмент.
 */
bool keepLongestContinuousSegment(QList<double> *values, QList<qint64> *timestamps)
{
    if (!values || !timestamps || values->size() != timestamps->size() || values->size() < 4)
        return false;

    QList<qint64> intervals;
    intervals.reserve(timestamps->size() - 1);
    for (int i = 1; i < timestamps->size(); ++i) {
        const qint64 interval = timestamps->at(i) - timestamps->at(i - 1);
        if (interval > 0)
            intervals.append(interval);
    }
    if (intervals.isEmpty())
        return false;

    std::sort(intervals.begin(), intervals.end());
    const qint64 median = intervals.at(intervals.size() / 2);
    if (median <= 0)
        return false;

    int bestFirst = 0;
    int bestLast = 0;
    int first = 0;
    for (int row = 1; row <= timestamps->size(); ++row) {
        const bool boundary = row == timestamps->size()
                              || double(timestamps->at(row) - timestamps->at(row - 1))
                                     > 3.0 * double(median);
        if (!boundary)
            continue;
        // При одинаковой длине берём свежий участок: он лучше соответствует текущему
        // состоянию устройства, которое пользователь и анализирует.
        if (row - first >= bestLast - bestFirst) {
            bestFirst = first;
            bestLast = row;
        }
        first = row;
    }

    if (bestFirst == 0 && bestLast == timestamps->size())
        return false;
    if (bestLast - bestFirst < 4)
        return false;

    *values = values->sliced(bestFirst, bestLast - bestFirst);
    *timestamps = timestamps->sliced(bestFirst, bestLast - bestFirst);
    return true;
}

} // namespace

PlotCanvas::PlotCanvas(IPanelHost *host, PlotModel *model, PlotViewState *view,
                       QWidget *parent)
    : QWidget(parent)
    , m_host(host)
    , m_model(model)
    , m_view(view)
{
    setObjectName(QStringLiteral("plotterChart"));
    // Перекрестие следует за курсором без нажатия: снимать значение зажатой кнопкой
    // неудобно, а другого смысла у простого движения здесь нет.
    setMouseTracking(true);
    setMinimumHeight(120);

    // Одиночный таймер, а не периодический: когда данные не идут, ничего не тикает и
    // спящий график не будит процессор впустую.
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setSingleShot(true);
    m_repaintTimer->setInterval(kRepaintIntervalMs);
    connect(m_repaintTimer, &QTimer::timeout, this, [this] {
        if (!m_dirty)
            return;
        m_dirty = false;
        update();
    });

    connect(m_model, &PlotModel::changed, this, &PlotCanvas::followNewData);
    // Состояние вида общее на все три холста, поэтому сдвиг окна в одном перерисовывает
    // остальные — это и означает «единый объект».
    connect(m_view, &PlotViewState::changed, this, &PlotCanvas::scheduleRepaint);
    connect(m_view, &PlotViewState::modeChanged, this, [this] { followNewData(); });

    createActions();
}

void PlotCanvas::scheduleRepaint()
{
    // Пауза останавливает захват новых точек в PlotterPlugin, а не взаимодействие с уже
    // записанным: масштаб, прокрутка и перекрестие должны сразу перерисовываться, пока
    // пользователь рассматривает зафиксированный фрагмент.
    m_dirty = true;
    if (!m_repaintTimer->isActive())
        m_repaintTimer->start();
}

void PlotCanvas::createActions()
{
    auto *pauseAction = new QAction(tr("Pause"), this);
    pauseAction->setCheckable(true);
    pauseAction->setChecked(m_view->paused());
    pauseAction->setIcon(m_host->icon(mdi::Pause, 18));
    pauseAction->setToolTip(tr("Pause collecting points for the plot"));
    connect(pauseAction, &QAction::toggled, m_view, &PlotViewState::setPaused);
    connect(m_view, &PlotViewState::pausedChanged, pauseAction, [pauseAction](bool paused) {
        const QSignalBlocker blocker(pauseAction);
        pauseAction->setChecked(paused);
    });
    addAction(pauseAction);

    // «В конец» — тот же смысл, что «Follow output» в терминале, только по горизонтали:
    // вернуться к свежим данным после того, как график утащили назад.
    auto *followAction = new QAction(tr("Jump to the newest data"), this);
    followAction->setCheckable(true);
    followAction->setChecked(m_view->following());
    followAction->setIcon(m_host->icon(mdi::ArrowCollapseRight, 18));
    connect(followAction, &QAction::toggled, m_view, &PlotViewState::setFollowing);
    connect(m_view, &PlotViewState::followingChanged, followAction, [followAction](bool on) {
        const QSignalBlocker blocker(followAction);
        followAction->setChecked(on);
    });
    addAction(followAction);

    auto *clearAction = new QAction(tr("Clear"), this);
    clearAction->setIcon(m_host->icon(mdi::Broom, 18));
    connect(clearAction, &QAction::triggered, m_model, &PlotModel::clearSamples);
    addAction(clearAction);
}

void PlotCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Двойной щелчок по полю — самый быстрый способ заморозить картинку, когда нужное
    // мелькнуло и вот-вот уедет за край.
    m_view->setPaused(!m_view->paused());
    event->accept();
}

void PlotCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Щелчок по вкладке оси делает ряд активным. Проверяется до перетаскивания:
        // вкладки лежат вне поля графика, и тащить за них нечего.
        const int series = seriesTabAt(event->pos());
        if (series >= 0) {
            selectAxis(series, event->modifiers());
            event->accept();
            return;
        }
    }

    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (plotArea().contains(event->pos())) {
        m_dragAxis = DragAxis::Both;
        setCursor(Qt::ClosedHandCursor);
    } else if (horizontalScaleRect().contains(event->pos())) {
        m_dragAxis = DragAxis::Horizontal;
        setCursor(Qt::SizeHorCursor);
    } else if (verticalScaleRect().contains(event->pos())) {
        m_dragAxis = DragAxis::Vertical;
        setCursor(Qt::SizeVerCursor);
    } else {
        QWidget::mousePressEvent(event);
        return;
    }

    m_dragging = true;
    m_dragOrigin = event->pos();
    m_dragFrom = m_view->windowFrom();
    m_dragOffset = m_view->verticalOffset();
    // Слежение снимается один раз, на нажатие, и только если тянут по времени: тащить
    // вертикальную шкалу можно и не сходя с живого потока.
    if (m_dragAxis != DragAxis::Vertical)
        m_view->setFollowing(false);
    event->accept();
}

void PlotCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        unsetCursor();

        // Проверка «доехали до правого края» делается по завершении жеста, а не
        // непрерывно: посреди перетаскивания она включала бы слежение прямо под рукой, и
        // окно уезжало бы в конец, пока его тянут.
        const SampleBuffer &samples = m_model->samples();
        if (m_dragAxis != DragAxis::Vertical && samples.sampleCount() >= 2) {
            if (const std::optional<qint64> last =
                    horizontalCoordinateAt(samples.sampleCount() - 1)) {
                m_view->snapToEnd(*last);
            }
        }

        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PlotCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QRect area = plotArea();

    if (m_dragging && area.width() > 0 && area.height() > 0) {
        // Сдвиг считается от точки нажатия, а не от прошлого события: так перетаскивание
        // не накапливает ошибку округления и график не уползает при возврате мыши назад.
        if (m_dragAxis != DragAxis::Vertical) {
            const int deltaX = event->pos().x() - m_dragOrigin.x();
            const qint64 duration = m_view->windowDuration();
            const qint64 shift =
                qint64(-double(deltaX) / double(area.width()) * double(duration));
            m_view->setWindow(m_dragFrom + shift, m_dragFrom + shift + duration);
        }

        if (m_dragAxis != DragAxis::Horizontal) {
            // Точка под курсором обязана оставаться под ним по обеим осям, иначе
            // перетаскивание ведёт себя как половина ожидаемого. Делится на масштаб,
            // потому что сдвиг задан долей полной шкалы, а видно из неё лишь 1/zoom часть.
            const int deltaY = event->pos().y() - m_dragOrigin.y();
            m_view->setVerticalOffset(m_dragOffset
                                      + double(deltaY) / double(area.height())
                                            / m_view->verticalZoom());
        }
        // Прямое управление обязано откликаться сразу: 16-мс очередь перерисовки хороша
        // для приходящих данных, но под рукой пользователя её задержка читается как
        // рывок. update() к тому же склеивает подряд идущие запросы сам.
        update();
        event->accept();
        return;
    }

    m_cursorX = area.contains(event->pos()) ? event->pos().x() : -1;
    update();
}

void PlotCanvas::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_cursorX = -1;
    update();
}

void PlotCanvas::wheelEvent(QWheelEvent *event)
{
    const QRect area = plotArea();
    if (area.width() < 2) {
        QWidget::wheelEvent(event);
        return;
    }

    // Трекпад даёт пиксели, мышь — щелчки по 120 восьмых долей градуса. Берём то, что
    // пришло, и копим остаток, иначе плавное движение пальцем идёт рывками.
    const QPoint pixels = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    double steps = 0.0;
    if (!pixels.isNull()) {
        m_wheelRemainder += QPointF(pixels);
        steps = (m_wheelRemainder.y() + m_wheelRemainder.x()) / 50.0;
        if (qAbs(steps) < 0.05) {
            // Остаток копится дальше — движение пальцем продолжается.
            event->accept();
            return;
        }
        m_wheelRemainder = QPointF();
    } else {
        steps = double(angle.y() != 0 ? angle.y() : angle.x()) / 120.0;
    }
    if (qFuzzyIsNull(steps))
        return;

    // Над самой шкалой модификатор не нужен: показывать пальцем на ось и есть способ
    // сказать, какую именно масштабировать.
    const QPoint where = event->position().toPoint();
    if (verticalScaleRect().contains(where)) {
        m_view->zoomY(steps > 0 ? 1.15 : 1.0 / 1.15);
        event->accept();
        return;
    }
    if (horizontalScaleRect().contains(where)) {
        const XTransform transform{m_view->windowFrom(), m_view->windowTo(),
                                   double(area.left()), double(area.width())};
        m_view->zoomX(steps > 0 ? 0.85 : 1.0 / 0.85,
                      transform.timeAt(event->position().x()));
        event->accept();
        return;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();

    if (modifiers.testFlag(Qt::ControlModifier)) {
        // Точка под курсором остаётся на месте — иначе приближение уводит из-под указателя
        // то самое место, ради которого его и делают.
        const XTransform transform{m_view->windowFrom(), m_view->windowTo(),
                                   double(area.left()), double(area.width())};
        const qint64 anchor = transform.timeAt(event->position().x());
        m_view->zoomX(steps > 0 ? 0.85 : 1.0 / 0.85, anchor);
    } else if (modifiers.testFlag(Qt::AltModifier)) {
        m_view->zoomY(steps > 0 ? 1.15 : 1.0 / 1.15);
    } else if (modifiers.testFlag(Qt::ShiftModifier)) {
        m_view->panY(steps * -0.05);
    } else {
        m_view->panBy(qint64(-steps * 0.1 * double(m_view->windowDuration())));
        const SampleBuffer &samples = m_model->samples();
        if (samples.sampleCount() >= 2) {
            if (const std::optional<qint64> last =
                    horizontalCoordinateAt(samples.sampleCount() - 1)) {
                m_view->snapToEnd(*last);
            }
        }
    }

    event->accept();
}

void PlotCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    // Единица полученного счётчика неизвестна, поэтому в этом случае пресеты задаются
    // числом делений, а не выдают себя за секунды.
    const QList<QPair<QString, qint64>> spans = horizontalCoordinateColumn() >= 0
                                                    ? QList<QPair<QString, qint64>>{
                                                          {tr("Last 100 counts"),
                                                           100 * Decimator::kCounterCoordinateScale},
                                                          {tr("Last 1K counts"),
                                                           1'000 * Decimator::kCounterCoordinateScale},
                                                          {tr("Last 10K counts"),
                                                           10'000 * Decimator::kCounterCoordinateScale},
                                                          {tr("Last 100K counts"),
                                                           100'000 * Decimator::kCounterCoordinateScale},
                                                      }
                                                    : QList<QPair<QString, qint64>>{
                                                          {tr("Last 1 s"), 1'000'000'000LL},
                                                          {tr("Last 10 s"), 10'000'000'000LL},
                                                          {tr("Last 1 min"), 60'000'000'000LL},
                                                          {tr("Last 10 min"), 600'000'000'000LL},
                                                      };
    for (const auto &[label, duration] : spans) {
        connect(menu.addAction(label), &QAction::triggered, this, [this, duration] {
            m_view->setWindowDuration(duration);
            m_view->setFollowing(true);
        });
    }

    connect(menu.addAction(tr("Whole buffer")), &QAction::triggered, this, [this] {
        const SampleBuffer &samples = m_model->samples();
        if (samples.sampleCount() < 2)
            return;
        const std::optional<qint64> first = horizontalCoordinateAt(0);
        const std::optional<qint64> last =
            horizontalCoordinateAt(samples.sampleCount() - 1);
        if (first && last && *last > *first) {
            m_view->setFollowing(false);
            m_view->setWindow(*first, *last);
        }
    });

    menu.addSeparator();

    QAction *follow = menu.addAction(tr("Follow new data"));
    follow->setCheckable(true);
    follow->setChecked(m_view->following());
    connect(follow, &QAction::toggled, m_view, &PlotViewState::setFollowing);

    QAction *pause = menu.addAction(tr("Pause"));
    pause->setCheckable(true);
    pause->setChecked(m_view->paused());
    connect(pause, &QAction::toggled, m_view, &PlotViewState::setPaused);

    menu.addSeparator();
    connect(menu.addAction(tr("Reset vertical zoom")), &QAction::triggered,
            m_view, &PlotViewState::resetVertical);
    connect(menu.addAction(tr("Clear")), &QAction::triggered,
            m_model, &PlotModel::clearSamples);

    menu.exec(event->globalPos());
}

QRect PlotCanvas::plotArea() const
{
    return rect().adjusted(m_leftMargin, kMarginTop, -kMarginRight, -kMarginBottom);
}

QRect PlotCanvas::verticalScaleRect() const
{
    const QRect area = plotArea();
    return QRect(0, area.top(), m_leftMargin, area.height());
}

QRect PlotCanvas::horizontalScaleRect() const
{
    const QRect area = plotArea();
    return QRect(area.left(), area.bottom(), area.width(), kMarginBottom);
}

QPixmap PlotCanvas::snapshot() const
{
    // Снимок рисуется в отступе устройства, а не в логических точках: на экране Retina
    // сохранённый в точках график выходит вдвое мельче и выглядит размытым.
    QPixmap pixmap(size() * devicePixelRatioF());
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    pixmap.fill(m_host->color(IPanelHost::ColorRole::Base));
    const_cast<PlotCanvas *>(this)->render(&pixmap);
    return pixmap;
}

bool PlotCanvas::saveImage(const QString &filePath)
{
    return snapshot().save(filePath, "PNG");
}

XTransform PlotCanvas::transformFor(const QRect &area) const
{
    // Ничего не меняет. Прежде отсюда вызывался clampTo(), то есть отрисовка правила
    // состояние вида: каждый кадр просил следующий, перерисовка не унималась никогда, а
    // при перетаскивании этот же вызов возвращал окно назад — рука тянула, а следующий
    // кадр отменял. Со стороны выглядело так, будто график двигается только на отпускание.
    return XTransform{m_view->windowFrom(), m_view->windowTo(), double(area.left()),
                      double(area.width())};
}

int PlotCanvas::horizontalCoordinateColumn() const
{
    switch (m_view->mode()) {
    case PlotViewState::Mode::TimeSeries:
    case PlotViewState::Mode::Cumulative:
    case PlotViewState::Mode::MultiPlot:
        return m_model->xAxisSeries();

    case PlotViewState::Mode::Xy:
    case PlotViewState::Mode::Histogram:
    case PlotViewState::Mode::Spectrum:
        return -1;
    }
    return -1;
}

std::optional<qint64> PlotCanvas::horizontalCoordinateAt(int row) const
{
    return Decimator::coordinateAt(m_model->samples(), row, horizontalCoordinateColumn());
}

void PlotCanvas::followNewData()
{
    const SampleBuffer &samples = m_model->samples();
    if (samples.sampleCount() >= 2) {
        const std::optional<qint64> first = horizontalCoordinateAt(0);
        const std::optional<qint64> last =
            horizontalCoordinateAt(samples.sampleCount() - 1);
        if (first && last && *last > *first) {
            const int coordinateColumn = horizontalCoordinateColumn();
            if (coordinateColumn != m_initializedCoordinateColumn) {
                // Единицы счётчика не обязаны быть наносекундами. При смене источника
                // сначала показываем весь его накопленный диапазон, а не старое окно из
                // другой системы координат.
                m_initializedCoordinateColumn = coordinateColumn;
                m_view->setWindow(*first, *last);
                m_view->setFollowing(true);
            } else {
                m_view->followTo(*first, *last);
            }
        }
    }
    scheduleRepaint();
}

YScale PlotCanvas::applyVertical(const PlotScales::Range &range, const QRect &area) const
{
    // Общий вертикальный масштаб двигает все ряды разом, сохраняя относительный вид: у
    // каждого своя размерность, и одинаковый множитель — единственное, что осмысленно
    // применить сразу ко всем.
    const double centre = (range.minimum + range.maximum) / 2.0
                          + m_view->verticalOffset() * (range.maximum - range.minimum);
    const double half = (range.maximum - range.minimum) / 2.0 / m_view->verticalZoom();

    return YScale{centre - half, centre + half, double(area.top()), double(area.height())};
}

QList<PlotCanvas::SeriesFrame> PlotCanvas::buildFrames(const QRect &area,
                                                       const XTransform &transform) const
{
    QList<SeriesFrame> frames;

    const Decimator::Accumulator accumulator =
        m_view->mode() == PlotViewState::Mode::Cumulative ? Decimator::Accumulator::RunningSum
                                                          : Decimator::Accumulator::None;
    const qint64 maximumConnectedGap =
        Decimator::maximumConnectedGap(m_model->samples(), transform, area.width(),
                                       horizontalCoordinateColumn());

    for (int index = 0; index < m_model->seriesCount(); ++index) {
        if (!m_model->series(index).visible || index == m_model->xAxisSeries())
            continue;

        SeriesFrame frame;
        frame.index = index;
        frame.reduced = Decimator::reduce(m_model->samples(), index, transform, area.width(),
                                          accumulator, maximumConnectedGap,
                                          horizontalCoordinateColumn());
        frames.append(frame);
    }

    // Пределы группы считаются после того, как все ряды сведены: объединение берётся по
    // видимым значениям, а их даёт тот же проход прореживания.
    PlotScales::Range group;
    for (const SeriesFrame &frame : frames) {
        if (m_view->isInScaleGroup(frame.index))
            group = PlotScales::merge(group, frame.reduced.visible);
    }

    for (SeriesFrame &frame : frames) {
        const PlotScales::Range resolved =
            PlotScales::resolve(m_model->series(frame.index), frame.reduced.visible,
                                m_view->isInScaleGroup(frame.index) ? group
                                                                    : PlotScales::Range{});
        frame.scale = applyVertical(resolved, area);
    }

    return frames;
}

int PlotCanvas::verticalAxisMargin(const QList<SeriesFrame> &frames) const
{
    const int labelled = labelledSeries(frames);
    if (labelled < 0)
        return kMinimumMarginLeft;

    YScale scale;
    for (const SeriesFrame &frame : frames) {
        if (frame.index == labelled) {
            scale = frame.scale;
            break;
        }
    }

    return valueAxisMargin(scale);
}

int PlotCanvas::valueAxisMargin(const YScale &scale) const
{
    const QFontMetrics metrics(font());
    int widest = 0;
    for (int i = 0; i <= kGridLines + 1; ++i) {
        const double fraction = double(i) / double(kGridLines + 1);
        const double value = scale.minimum + fraction * (scale.maximum - scale.minimum);
        widest = qMax(widest, metrics.horizontalAdvance(PlotFormat::number(value,
                                                                             kAxisLabelDigits)));
    }

    // Подпись, зазор, вкладка ряда и два пикселя воздуха. Без динамики большое число
    // заходило на поле графика и накладывалось на кривую ровно там, где его читали.
    return qMax(kMinimumMarginLeft, widest + kAxisLabelGap + kAxisTabWidth + 4);
}

int PlotCanvas::labelledSeries(const QList<SeriesFrame> &frames) const
{
    if (frames.isEmpty())
        return -1;

    for (const SeriesFrame &frame : frames) {
        if (frame.index == m_view->activeSeries())
            return frame.index;
    }
    // Активного нет или он скрыт — подписываем первый видимый: пустая шкала не отвечает ни
    // на один вопрос.
    return frames.first().index;
}

QRect PlotCanvas::seriesTabRect(int position, int count, const QRect &area) const
{
    if (count <= 0)
        return {};

    const int height = area.height() / count;
    return QRect(area.left() - kAxisTabWidth - 1, area.top() + position * height,
                 kAxisTabWidth, qMax(2, height - 2));
}

int PlotCanvas::seriesTabAt(const QPoint &point) const
{
    const QRect area = plotArea();
    for (int position = 0; position < m_visibleOrder.size(); ++position) {
        // Прямоугольник расширяется по горизонтали: попасть в полоску шириной пять
        // пикселей мышью трудно, а промах здесь ничего не ломает.
        if (seriesTabRect(position, int(m_visibleOrder.size()), area)
                .adjusted(-3, 0, 3, 0)
                .contains(point)) {
            return m_visibleOrder.at(position);
        }
    }
    return -1;
}

void PlotCanvas::selectAxis(int series, Qt::KeyboardModifiers modifiers)
{
    QList<int> group = m_view->selectionGroup();
    bool makeActive = true;

    if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier)) {
        if (group.contains(series)) {
            group.removeAll(series);
            // Снятую ось нельзя делать активной: слева была бы подписана шкала ряда,
            // который в общую шкалу больше не входит.
            makeActive = false;
        } else {
            group.append(series);
        }
    } else if (modifiers.testFlag(Qt::ShiftModifier) && m_view->activeSeries() >= 0) {
        const int from = int(m_visibleOrder.indexOf(m_view->activeSeries()));
        const int to = int(m_visibleOrder.indexOf(series));
        if (from >= 0 && to >= 0) {
            group.clear();
            for (int i = qMin(from, to); i <= qMax(from, to); ++i)
                group.append(m_visibleOrder.at(i));
        } else {
            group = {series};
        }
    } else {
        group = {series};
    }

    m_view->setSelectionGroup(group);
    if (makeActive)
        m_view->setActiveSeries(series);
}

void PlotCanvas::drawFrame(QPainter &painter, const QRect &area, const XTransform &transform,
                           const QList<SeriesFrame> &frames) const
{
    painter.setPen(m_host->color(IPanelHost::ColorRole::Border));
    painter.drawRect(area);

    // Слева подписана шкала активного ряда — того, что нарисован ярче остальных. Подписать
    // «какую-нибудь» значило бы дать числа, не относящиеся ни к одной видимой кривой.
    const int labelled = labelledSeries(frames);
    YScale scale;
    QColor labelColor = m_host->color(IPanelHost::ColorRole::TextMuted);
    for (const SeriesFrame &frame : frames) {
        if (frame.index != labelled)
            continue;
        scale = frame.scale;
        if (m_model->seriesCount() > 1)
            labelColor = QColor::fromRgba(m_model->series(frame.index).color);
    }

    if (labelled >= 0)
        drawValueAxis(painter, area, scale, labelColor);
    drawHorizontalAxis(painter, area, transform);

    // Вкладки осей: по одной на видимый ряд, у самого края поля. Щелчок по вкладке делает
    // ряд активным, то есть переносит на него подписи слева, — это и есть «нажать на ось».
    if (frames.size() > 1) {
        for (int position = 0; position < frames.size(); ++position) {
            const SeriesFrame &frame = frames.at(position);

            // Полным цветом — участники общей шкалы, а без группы просто подписанный ряд.
            // Остальные приглушены: так по одному взгляду на поле видно, чьи оси сведены.
            const bool participates = m_view->hasScaleGroup()
                                          ? m_view->isInScaleGroup(frame.index)
                                          : frame.index == labelled;

            QColor colour = QColor::fromRgba(m_model->series(frame.index).color);
            if (!participates)
                colour.setAlpha(kDimmedAlpha);

            QRect tab = seriesTabRect(position, int(frames.size()), area);
            painter.setPen(Qt::NoPen);
            painter.setBrush(colour);
            painter.drawRect(tab);
        }
        painter.setBrush(Qt::NoBrush);
    }
}

void PlotCanvas::drawValueAxis(QPainter &painter, const QRect &area, const YScale &scale,
                               const QColor &labelColor) const
{
    const QColor grid = m_host->color(IPanelHost::ColorRole::Border);
    const QFontMetrics metrics(font());
    for (int i = 0; i <= kGridLines + 1; ++i) {
        const double fraction = double(i) / double(kGridLines + 1);
        const int y = area.bottom() - int(fraction * area.height());
        if (i > 0 && i <= kGridLines) {
            painter.setPen(grid);
            painter.drawLine(area.left() + 1, y, area.right() - 1, y);
        }

        const double value = scale.minimum + fraction * (scale.maximum - scale.minimum);
        const QString label = PlotFormat::number(value, kAxisLabelDigits);
        const int labelRight = area.left() - kAxisTabWidth - kAxisLabelGap;
        painter.setPen(labelColor);
        painter.drawText(labelRight - metrics.horizontalAdvance(label),
                         y + metrics.ascent() / 2, label);
        painter.setPen(grid);
        painter.drawLine(area.left() - 3, y, area.left(), y);
    }
}

void PlotCanvas::drawHorizontalAxis(QPainter &painter, const QRect &area,
                                    const XTransform &transform) const
{
    const QFontMetrics metrics(font());
    const qint64 duration = transform.to - transform.from;
    const int axisTextY = area.bottom() + metrics.ascent() + 3;
    const bool counterAxis = horizontalCoordinateColumn() >= 0;
    const auto formatX = [counterAxis, &transform, duration](qint64 value) {
        return counterAxis ? PlotFormat::number(
                                 double(value) / double(Decimator::kCounterCoordinateScale),
                                 kAxisLabelDigits)
                           : formatTime(value - transform.from, duration);
    };
    const QString left = formatX(transform.from);
    const QString right = formatX(transform.to);
    const int leftWidth = metrics.horizontalAdvance(left);
    const int rightWidth = metrics.horizontalAdvance(right);
    const bool endpointsFit = leftWidth + rightWidth + kHorizontalLabelGap < area.width();

    painter.setPen(m_host->color(IPanelHost::ColorRole::TextMuted));
    painter.drawLine(area.left(), area.bottom(), area.left(), area.bottom() + 3);
    painter.drawLine(area.right(), area.bottom(), area.right(), area.bottom() + 3);
    if (endpointsFit)
        painter.drawText(area.left(), axisTextY, left);
    painter.drawText(area.right() - rightWidth, axisTextY, right);

    const QString middle = formatX(transform.from + duration / 2);
    if (endpointsFit && leftWidth + rightWidth + metrics.horizontalAdvance(middle)
                           + 2 * kHorizontalLabelGap < area.width()) {
        const int x = area.center().x();
        painter.drawLine(x, area.bottom(), x, area.bottom() + 3);
        painter.drawText(x - metrics.horizontalAdvance(middle) / 2, axisTextY, middle);
    }
}

void PlotCanvas::drawNumericHorizontalAxis(QPainter &painter, const QRect &area, double minimum,
                                           double maximum) const
{
    const QFontMetrics metrics(font());
    const int axisTextY = area.bottom() + metrics.ascent() + 3;
    const auto drawTick = [&](double value, Qt::Alignment alignment) {
        const QString label = PlotFormat::number(value, kAxisLabelDigits);
        const int width = metrics.horizontalAdvance(label);
        int x = area.left() + qRound((value - minimum) / (maximum - minimum) * area.width());
        int labelX = x - width / 2;
        if (alignment.testFlag(Qt::AlignLeft))
            labelX = x;
        else if (alignment.testFlag(Qt::AlignRight))
            labelX = x - width;
        painter.drawLine(x, area.bottom(), x, area.bottom() + 3);
        painter.drawText(labelX, axisTextY, label);
    };

    const QString left = PlotFormat::number(minimum, kAxisLabelDigits);
    const QString right = PlotFormat::number(maximum, kAxisLabelDigits);
    const bool endpointsFit = metrics.horizontalAdvance(left) + metrics.horizontalAdvance(right)
                              + kHorizontalLabelGap < area.width();
    painter.setPen(m_host->color(IPanelHost::ColorRole::TextMuted));
    if (endpointsFit)
        drawTick(minimum, Qt::AlignLeft);
    drawTick(maximum, Qt::AlignRight);

    const double middle = (minimum + maximum) / 2.0;
    const QString middleLabel = PlotFormat::number(middle, kAxisLabelDigits);
    if (endpointsFit && metrics.horizontalAdvance(left) + metrics.horizontalAdvance(right)
                           + metrics.horizontalAdvance(middleLabel) + 2 * kHorizontalLabelGap
                               < area.width()) {
        drawTick(middle, Qt::AlignHCenter);
    }
}

void PlotCanvas::drawCurve(QPainter &painter, const QRect &area, const SeriesFrame &frame,
                           const QColor &colour, bool withFill) const
{
    if (frame.reduced.columns.isEmpty())
        return;

    // Прореживатель добавляет соседние точки за окном, чтобы линия естественно пересекала
    // его край. Они нужны геометрии, но не должны заходить на подписи шкал или соседние
    // мини-графики.
    painter.save();
    painter.setClipRect(area);

    const bool dense = frame.reduced.columns.size() >= area.width();

    // Отрезки рисуются по одному: пропуск в данных рвёт кривую, и соединять её через
    // разрыв значило бы показывать то, чего устройство не присылало.
    for (int run = 0; run < frame.reduced.runStarts.size(); ++run) {
        const int first = frame.reduced.runStarts.at(run);
        const int last = (run + 1 < frame.reduced.runStarts.size())
                             ? frame.reduced.runStarts.at(run + 1)
                             : int(frame.reduced.columns.size());
        if (last - first < 1)
            continue;

        QPolygonF polyline;
        polyline.reserve((last - first) * 2);
        for (int i = first; i < last; ++i) {
            const Decimator::Column &column = frame.reduced.columns.at(i);
            const double x = area.left() + column.x;
            // Сверху вниз в экранных координатах: порядок один во всех колонках, поэтому
            // соседние отрезки смыкаются, а не расходятся зигзагом.
            polyline.append(QPointF(x, frame.scale.yOf(column.maximum)));
            if (!qFuzzyCompare(column.minimum, column.maximum))
                polyline.append(QPointF(x, frame.scale.yOf(column.minimum)));
        }
        if (polyline.isEmpty())
            continue;

        if (polyline.size() == 1) {
            // У редкого, но равномерного потока одна точка не обязана означать ошибку.
            // Рисуем сам факт измерения, но не выдумываем отрезок до следующего.
            painter.setPen(QPen(colour, dense ? 1.0 : 2.0));
            painter.drawPoint(polyline.first());
            continue;
        }

        if (withFill) {
            // Заливается верхняя огибающая, а не сама полилиния: прореженная полилиния
            // пересекает себя в каждой колонке, и растеризатор разбирал бы тысячи
            // самопересечений на каждой строке развёртки.
            QPolygonF filled;
            filled.reserve(last - first + 2);
            for (int i = first; i < last; ++i) {
                const Decimator::Column &column = frame.reduced.columns.at(i);
                filled.append(QPointF(area.left() + column.x,
                                      frame.scale.yOf(column.maximum)));
            }
            filled.append(QPointF(filled.last().x(), area.bottom()));
            filled.append(QPointF(filled.first().x(), area.bottom()));

            QColor fill = colour;
            fill.setAlpha(kFillAlpha);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawPolygon(filled);
            painter.setBrush(Qt::NoBrush);
        }

        // Дробная толщина без сглаживания всё равно округлится до целой.
        painter.setPen(QPen(colour, dense ? 1.0 : 1.5));
        painter.drawPolyline(polyline);
    }

    painter.restore();
}

void PlotCanvas::drawTimeSeries(QPainter &painter, const QRect &area,
                                const XTransform &transform,
                                const QList<SeriesFrame> &frames) const
{
    Q_UNUSED(transform);

    const int labelled = labelledSeries(frames);
    const bool withFill = frames.size() <= kMaxFilledSeries;

    // Активный ряд рисуется последним, чтобы лечь поверх остальных.
    QList<const SeriesFrame *> order;
    for (const SeriesFrame &frame : frames) {
        if (frame.index != labelled)
            order.append(&frame);
    }
    for (const SeriesFrame &frame : frames) {
        if (frame.index == labelled)
            order.append(&frame);
    }

    for (const SeriesFrame *frame : order) {
        QColor colour = QColor::fromRgba(m_model->series(frame->index).color);
        // Приглушение альфой, а не сменой оттенка: тусклый ряд обязан оставаться
        // узнаваемым по цвету — иначе таблица сбоку перестанет соответствовать графику.
        const bool dimmed = m_view->hasScaleGroup() && !m_view->isInScaleGroup(frame->index);
        if (dimmed)
            colour.setAlpha(kDimmedAlpha);

        drawCurve(painter, area, *frame, colour, withFill && !dimmed);
    }
}

void PlotCanvas::drawSeries(QPainter &painter, const QRect &area, const XTransform &transform,
                            const QList<SeriesFrame> &frames) const
{
    drawTimeSeries(painter, area, transform, frames);
}

void PlotCanvas::drawXy(QPainter &painter, const QRect &area) const
{
    // По выбранной X время не участвует вовсе, по обеим осям идут значения. Колонка для X
    // берётся из настройки вида; без неё показывать нечего.
    const int xColumn = m_model->xAxisSeries();
    if (xColumn < 0) {
        painter.setPen(m_host->color(IPanelHost::ColorRole::TextMuted));
        painter.drawText(area, Qt::AlignCenter,
                         tr("Pick a column for the X axis to see a phase plot"));
        return;
    }

    const SampleBuffer &samples = m_model->samples();
    const SampleBuffer::ColumnStats xStats = samples.stats(xColumn);
    if (xStats.finiteCount < 2)
        return;

    const PlotScales::Range xRange =
        PlotScales::padded({xStats.minimum, xStats.maximum, true});

    const auto xOf = [&](double value) {
        return area.left()
               + (value - xRange.minimum) / (xRange.maximum - xRange.minimum) * area.width();
    };

    const int labelled = subjectSeries();
    if (labelled >= 0) {
        const SampleBuffer::ColumnStats stats = samples.stats(labelled);
        const PlotScales::Range range = PlotScales::resolve(
            m_model->series(labelled), {stats.minimum, stats.maximum, stats.finiteCount > 0}, {});
        drawValueAxis(painter, area, applyVertical(range, area),
                      QColor::fromRgba(m_model->series(labelled).color));
    }
    drawNumericHorizontalAxis(painter, area, xRange.minimum, xRange.maximum);

    painter.save();
    painter.setClipRect(area);
    for (int index = 0; index < m_model->seriesCount(); ++index) {
        if (!m_model->series(index).visible || index == xColumn)
            continue;

        const SampleBuffer::ColumnStats stats = samples.stats(index);
        if (stats.finiteCount < 2)
            continue;

        const PlotScales::Range range = PlotScales::resolve(
            m_model->series(index), {stats.minimum, stats.maximum, true}, {});
        const YScale scale = applyVertical(range, area);

        // Точки соединяются в порядке прихода: именно последовательность и делает из
        // облака точек фазовый портрет.
        QPolygonF polyline;
        polyline.reserve(samples.sampleCount());
        for (int row = 0; row < samples.sampleCount(); ++row) {
            const double x = samples.at(row, xColumn);
            const double y = samples.at(row, index);
            if (!qIsFinite(x) || !qIsFinite(y)) {
                // Разрыв: пропуск по любой из осей рвёт линию.
                if (polyline.size() >= 2)
                    painter.drawPolyline(polyline);
                polyline.clear();
                continue;
            }
            polyline.append(QPointF(xOf(x), scale.yOf(y)));
        }

        painter.setPen(QPen(QColor::fromRgba(m_model->series(index).color), 1.0));
        if (polyline.size() >= 2)
            painter.drawPolyline(polyline);
    }
    painter.restore();
}

void PlotCanvas::drawMultiPlot(QPainter &painter, const QRect &area,
                               const XTransform &transform)
{
    QList<int> visible;
    for (int index = 0; index < m_model->seriesCount(); ++index) {
        if (m_model->series(index).visible && index != m_model->xAxisSeries())
            visible.append(index);
    }
    if (visible.isEmpty())
        return;

    const QColor grid = m_host->color(IPanelHost::ColorRole::Border);
    painter.setPen(grid);
    painter.drawRect(area);
    drawHorizontalAxis(painter, area, transform);

    const int bandHeight = area.height() / int(visible.size());
    const QFontMetrics metrics(font());
    const qint64 maximumConnectedGap =
        Decimator::maximumConnectedGap(m_model->samples(), transform, area.width(),
                                       horizontalCoordinateColumn());

    for (int position = 0; position < visible.size(); ++position) {
        const int index = visible.at(position);
        const QRect band(area.left(), area.top() + position * bandHeight, area.width(),
                         bandHeight - 2);
        if (band.height() < 4)
            continue;

        // У каждой полосы своя шкала по своим значениям: ради этого мультиплот и нужен —
        // ряды разных величин перестают давить друг друга.
        SeriesFrame frame;
        frame.index = index;
        frame.reduced = Decimator::reduce(m_model->samples(), index, transform, band.width(),
                                          Decimator::Accumulator::None, maximumConnectedGap,
                                          horizontalCoordinateColumn());
        frame.scale = applyVertical(PlotScales::resolve(m_model->series(index),
                                                        frame.reduced.visible, {}),
                                    band);

        if (band.height() >= 2 * metrics.height() + 4) {
            painter.setPen(grid);
            painter.drawLine(band.left() + 1, band.center().y(), band.right() - 1,
                             band.center().y());
        }
        if (position + 1 < visible.size()) {
            painter.setPen(grid);
            painter.drawLine(band.left(), band.bottom() + 1, band.right(), band.bottom() + 1);
        }

        const QColor colour = QColor::fromRgba(m_model->series(index).color);
        drawCurve(painter, band, frame, colour, true);

        // У полосы две крайние метки Y: полная сетка в каждой из пяти-десяти полос была
        // бы шумом, а минимум и максимум всё ещё дают масштаб, в котором читается кривая.
        if (band.height() >= 2 * metrics.height() + 4) {
            const QString maximum = PlotFormat::number(frame.scale.maximum, kAxisLabelDigits);
            const QString minimum = PlotFormat::number(frame.scale.minimum, kAxisLabelDigits);
            const int labelRight = band.left() - kAxisTabWidth - kAxisLabelGap;
            painter.setPen(colour);
            painter.drawText(labelRight - metrics.horizontalAdvance(maximum),
                             band.top() + metrics.ascent(), maximum);
            painter.drawText(labelRight - metrics.horizontalAdvance(minimum), band.bottom(),
                             minimum);
        }

        // Имя лежит на непрозрачной плашке: текст нельзя класть прямо на кривую, потому
        // что на контрастном участке он исчезает ровно в самом информативном месте.
        const QString name = m_model->series(index).name;
        const int labelWidth = metrics.horizontalAdvance(name) + 8;
        const QRect labelRect(band.left() + 2, band.top() + 2,
                              qMin(labelWidth, qMax(1, band.width() - 4)), metrics.height() + 2);
        painter.fillRect(labelRect, m_host->color(IPanelHost::ColorRole::Base));
        painter.setPen(colour);
        painter.drawText(labelRect.adjusted(3, 0, -3, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(name, Qt::ElideRight, qMax(0, labelRect.width() - 6)));

    }
}

int PlotCanvas::subjectSeries() const
{
    // Гистограмма и спектр одномерны: они отвечают на вопрос об **одном** ряде. Шесть
    // наложенных гистограмм не читаются, поэтому берётся активный ряд — тот, чья шкала
    // подписана слева и чья строка выделена в таблице.
    const int active = m_view->activeSeries();
    if (active >= 0 && active < m_model->seriesCount() && m_model->series(active).visible
        && active != m_model->xAxisSeries()) {
        return active;
    }

    for (int index = 0; index < m_model->seriesCount(); ++index) {
        if (m_model->series(index).visible && index != m_model->xAxisSeries())
            return index;
    }
    return -1;
}

void PlotCanvas::drawEmptySubjectName(QPainter &painter, const QRect &area, int series) const
{
    painter.setPen(QColor::fromRgba(m_model->series(series).color));
    painter.drawText(area, Qt::AlignCenter, m_model->series(series).name);
}

void PlotCanvas::visibleValues(QList<double> *values, QList<qint64> *timestamps) const
{
    const int index = subjectSeries();
    if (index < 0)
        return;

    const SampleBuffer &samples = m_model->samples();
    const int first = Decimator::lowerBound(samples, m_view->windowFrom());

    for (int row = first; row < samples.sampleCount(); ++row) {
        const qint64 stamp = samples.timestamp(row);
        if (stamp > m_view->windowTo())
            break;
        if (values)
            values->append(samples.at(row, index));
        if (timestamps)
            timestamps->append(stamp);
    }
}

void PlotCanvas::drawHistogram(QPainter &painter, const QRect &area) const
{
    const int index = subjectSeries();
    if (index < 0)
        return;

    QList<double> values;
    visibleValues(&values, nullptr);

    const QColor colour = QColor::fromRgba(m_model->series(index).color);
    const Histogram::Bins bins = Histogram::bins(values);
    if (bins.counts.isEmpty() || bins.total == 0) {
        drawEmptySubjectName(painter, area, index);
        return;
    }

    const QColor muted = m_host->color(IPanelHost::ColorRole::TextMuted);
    const QFontMetrics metrics(font());

    int tallest = 1;
    for (const int count : bins.counts)
        tallest = qMax(tallest, count);
    // Частота — самостоятельная величина: наследовать сдвиг/масштаб временной развёртки
    // значило бы оставить снизу пустое место и исказить форму распределения.
    const YScale countScale{0.0, double(tallest), double(area.top()), double(area.height())};
    drawValueAxis(painter, area, countScale, muted);
    drawNumericHorizontalAxis(painter, area, bins.minimum, bins.maximum);

    const double binWidth = bins.width();
    const auto xOf = [&](double value) {
        return area.left()
               + (value - bins.minimum) / (bins.maximum - bins.minimum) * area.width();
    };

    // Столбцы: доля попавших в корзину от самой населённой.
    QColor fill = colour;
    fill.setAlpha(150);
    painter.save();
    painter.setClipRect(area);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    for (int i = 0; i < bins.counts.size(); ++i) {
        const double left = xOf(bins.minimum + double(i) * binWidth);
        const double right = xOf(bins.minimum + double(i + 1) * binWidth);
        const double top = countScale.yOf(double(bins.counts.at(i)));
        const double bottom = countScale.yOf(0.0);
        painter.drawRect(QRectF(left, qMin(top, bottom), qMax(1.0, right - left - 1.0),
                                qAbs(bottom - top)));
    }
    painter.setBrush(Qt::NoBrush);

    const Histogram::Normal normal = Histogram::fitNormal(values);
    if (!normal.valid || normal.sigma <= 0.0) {
        painter.restore();
        return;
    }

    // Кривая нормального распределения масштабируется к площади столбцов: иначе плотность
    // (единицы на величину) и счётчики (штуки) оказались бы на одной оси без общей меры.
    const double scale = double(bins.total) * binWidth;
    QPolygonF curve;
    curve.reserve(area.width());
    for (int x = 0; x < area.width(); ++x) {
        const double value =
            bins.minimum + double(x) / double(area.width()) * (bins.maximum - bins.minimum);
        const double count = Histogram::normalDensity(normal, value) * scale;
        curve.append(QPointF(area.left() + x, countScale.yOf(count)));
    }
    painter.setPen(QPen(colour.lighter(140), 1.5));
    painter.drawPolyline(curve);

    // Вертикали в среднем и на ±σ: по ним читается и центр, и разброс.
    const QList<double> marks = {
        normal.mean,
        normal.mean - normal.sigma,
        normal.mean + normal.sigma,
    };
    for (const double value : marks) {
        if (value < bins.minimum || value > bins.maximum)
            continue;
        const double x = xOf(value);
        painter.setPen(QPen(muted, 1, Qt::DashLine));
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    painter.restore();

    const QString summary = tr("μ %1, σ %2")
                                .arg(PlotFormat::number(normal.mean, 4),
                                     PlotFormat::number(normal.sigma, 4));
    if (metrics.horizontalAdvance(summary) <= area.width() - 8) {
        const QRect summaryRect(area.right() - metrics.horizontalAdvance(summary) - 6,
                                area.top() + 2, metrics.horizontalAdvance(summary) + 4,
                                metrics.height() + 2);
        painter.fillRect(summaryRect, m_host->color(IPanelHost::ColorRole::Base));
        painter.setPen(muted);
        painter.drawText(summaryRect, Qt::AlignCenter, summary);
    }
}

void PlotCanvas::drawSpectrum(QPainter &painter, const QRect &area) const
{
    const int index = subjectSeries();
    if (index < 0)
        return;

    QList<double> values;
    QList<qint64> stamps;
    visibleValues(&values, &stamps);

    keepLongestContinuousSegment(&values, &stamps);
    const Spectrum::Result result = Spectrum::computeFromSamples(values, stamps);
    const QColor muted = m_host->color(IPanelHost::ColorRole::TextMuted);
    const QColor colour = QColor::fromRgba(m_model->series(index).color);

    if (!result.isValid()) {
        drawEmptySubjectName(painter, area, index);
        return;
    }

    double tallest = 0.0;
    for (const double value : result.magnitude)
        tallest = qMax(tallest, value);
    if (tallest <= 0.0) {
        drawEmptySubjectName(painter, area, index);
        return;
    }

    // Амплитуда спектра так же не связана с вертикальным масштабом исходного ряда.
    const YScale amplitudeScale{0.0, tallest, double(area.top()), double(area.height())};
    drawValueAxis(painter, area, amplitudeScale, muted);
    drawNumericHorizontalAxis(painter, area, 0.0, result.binHz * double(result.magnitude.size()));

    QColor fill = colour;
    fill.setAlpha(150);
    painter.save();
    painter.setClipRect(area);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);

    const int bins = int(result.magnitude.size());
    for (int i = 0; i < bins; ++i) {
        const double left = area.left() + double(i) / double(bins) * area.width();
        const double right = area.left() + double(i + 1) / double(bins) * area.width();
        const double top = amplitudeScale.yOf(result.magnitude.at(i));
        const double bottom = amplitudeScale.yOf(0.0);
        painter.drawRect(
            QRectF(left, qMin(top, bottom), qMax(1.0, right - left), qAbs(bottom - top)));
    }
    painter.setBrush(Qt::NoBrush);
    painter.restore();
}

void PlotCanvas::drawCursor(QPainter &painter, const QRect &area, const XTransform &transform,
                            const QList<SeriesFrame> &frames) const
{
    if (m_cursorX < 0 || frames.isEmpty())
        return;

    const SampleBuffer &samples = m_model->samples();
    if (samples.sampleCount() < 1)
        return;

    // Отсчёт под курсором ищется в той же горизонтальной системе координат, что и линия:
    // иначе график по счётчику показывал бы под курсором значение соседнего времени.
    const qint64 coordinate = transform.timeAt(m_cursorX);
    const int coordinateColumn = horizontalCoordinateColumn();
    int row = Decimator::lowerBound(samples, coordinate, coordinateColumn);
    row = qBound(0, row, samples.sampleCount() - 1);
    const std::optional<qint64> current = Decimator::coordinateAt(samples, row,
                                                                    coordinateColumn);
    const std::optional<qint64> previous = row > 0
                                                ? Decimator::coordinateAt(samples, row - 1,
                                                                          coordinateColumn)
                                                : std::nullopt;
    if (previous && current && qAbs(*previous - coordinate) < qAbs(*current - coordinate))
        --row;

    const int x = m_cursorX;

    painter.setPen(QPen(m_host->color(IPanelHost::ColorRole::TextMuted), 1, Qt::DashLine));
    painter.drawLine(x, area.top(), x, area.bottom());

    const QFontMetrics metrics(font());
    // Значение X привязано к самому указателю, а не к ближайшей точке: в разрыве потока
    // именно положение курсора отвечает на вопрос «в какой координате данных нет».
    const QString xLabel = coordinateColumn >= 0
                               ? PlotFormat::number(
                                     double(coordinate)
                                         / double(Decimator::kCounterCoordinateScale),
                                     kAxisLabelDigits)
                               : formatTime(coordinate - transform.from,
                                            transform.to - transform.from);
    const int xLabelWidth = metrics.horizontalAdvance(xLabel) + 6;
    const int xLabelMaximum = qMax(area.left(), area.right() - xLabelWidth);
    const int xLabelLeft = qBound(area.left(), x - xLabelWidth / 2, xLabelMaximum);
    const QRect xLabelRect(xLabelLeft, area.bottom() + 2, xLabelWidth, metrics.height() + 3);
    painter.fillRect(xLabelRect, m_host->color(IPanelHost::ColorRole::Base));
    painter.setPen(m_host->color(IPanelHost::ColorRole::ControlBorder));
    painter.drawRect(xLabelRect);
    painter.setPen(m_host->color(IPanelHost::ColorRole::TextMuted));
    painter.drawText(xLabelRect, Qt::AlignCenter, xLabel);

    int textY = area.top() + metrics.height();
    const qint64 pixelDuration = qMax<qint64>(
        1, (transform.to - transform.from) / qMax(1, area.width()) * 3);
    const std::optional<qint64> rowCoordinate =
        Decimator::coordinateAt(samples, row, coordinateColumn);
    const bool hasData = rowCoordinate && qAbs(*rowCoordinate - coordinate) <= pixelDuration;

    if (!hasData) {
        const QString label = tr("No data");
        const int width = metrics.horizontalAdvance(label);
        const int labelX = (x + 8 + width < area.right()) ? x + 8 : x - 8 - width;
        painter.setPen(m_host->color(IPanelHost::ColorRole::TextMuted));
        painter.drawText(labelX, textY, label);
        return;
    }

    bool anyValue = false;
    for (const SeriesFrame &frame : frames) {
        const double value = samples.at(row, frame.index);
        if (!qIsFinite(value))
            continue;

        anyValue = true;

        const QString label = QStringLiteral("%1  %2")
                                  .arg(m_model->series(frame.index).name,
                                       PlotFormat::number(value, kLabelDigits));

        // Подпись уходит на ту сторону от курсора, где больше места: иначе у правого края
        // она обрезается ровно тогда, когда смотрят на свежие данные.
        const int width = metrics.horizontalAdvance(label);
        const int labelX = (x + 8 + width < area.right()) ? x + 8 : x - 8 - width;

        painter.setPen(QColor::fromRgba(m_model->series(frame.index).color));
        painter.drawText(labelX, textY, label);
        textY += metrics.height();
    }

    if (!anyValue) {
        const QString label = tr("No data");
        const int width = metrics.horizontalAdvance(label);
        const int labelX = (x + 8 + width < area.right()) ? x + 8 : x - 8 - width;
        painter.setPen(m_host->color(IPanelHost::ColorRole::TextMuted));
        painter.drawText(labelX, textY, label);
    }
}

void PlotCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    // Сглаживание выключено намеренно и безусловно — см. замер в описании класса. Сетка и
    // рамка от этого не страдают: они из горизонтальных и вертикальных отрезков. Текст
    // сглаживается отдельным QPainter::TextAntialiasing, он здесь не трогается.
    QPainter painter(this);
    painter.fillRect(rect(), m_host->color(IPanelHost::ColorRole::Base));

    QRect area = plotArea();
    if (area.width() < 2 || area.height() < 2)
        return;

    if (m_model->seriesCount() == 0) {
        painter.setPen(m_host->color(IPanelHost::ColorRole::TextMuted));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Waiting for numeric lines in the output"));
        return;
    }

    XTransform transform = transformFor(area);
    QList<SeriesFrame> frames = buildFrames(area, transform);

    // Ширина поля оси зависит от самих чисел. Повторно сводим ряды только если она реально
    // изменилась: преобразование X меняется вместе с областью, а рисовать старые колонки
    // в новой геометрии означало бы сдвинуть данные относительно шкалы.
    int axisMargin = verticalAxisMargin(frames);
    switch (m_view->mode()) {
    case PlotViewState::Mode::Xy: {
        const int index = subjectSeries();
        if (index >= 0) {
            const SampleBuffer::ColumnStats stats = m_model->samples().stats(index);
            const PlotScales::Range range = PlotScales::resolve(
                m_model->series(index), {stats.minimum, stats.maximum, stats.finiteCount > 0},
                {});
            axisMargin = valueAxisMargin(applyVertical(range, area));
        }
        break;
    }

    case PlotViewState::Mode::Histogram:
    case PlotViewState::Mode::Spectrum:
        // В этих режимах ось Y выводит производные величины (частоты либо амплитуды),
        // поэтому ширину нельзя брать у исходного ряда. Обычного компактного поля
        // достаточно для четырёх значащих цифр; прежний резерв в 96 логических точек на
        // Retina съедал почти двести пикселей самого графика.
        axisMargin = kMinimumMarginLeft;
        break;

    case PlotViewState::Mode::MultiPlot:
        for (const SeriesFrame &frame : frames)
            axisMargin = qMax(axisMargin, valueAxisMargin(frame.scale));
        break;

    case PlotViewState::Mode::TimeSeries:
    case PlotViewState::Mode::Cumulative:
        break;
    }

    const int modeIndex = int(m_view->mode());
    Q_ASSERT(modeIndex >= 0 && modeIndex < int(m_axisMargins.size()));
    int &stableAxisMargin = m_axisMargins.at(size_t(modeIndex));
    if (m_view->mode() == PlotViewState::Mode::Histogram
        || m_view->mode() == PlotViewState::Mode::Spectrum) {
        // Шкала специальных режимов не связана с исходным рядом. Нельзя позволить ей
        // унаследовать однажды выросшее поле временной развёртки.
        stableAxisMargin = axisMargin;
    } else {
        stableAxisMargin = qMax(stableAxisMargin, axisMargin);
    }
    axisMargin = qMax(kMinimumMarginLeft, stableAxisMargin);
    if (axisMargin != m_leftMargin) {
        m_leftMargin = axisMargin;
        area = plotArea();
        if (area.width() < 2 || area.height() < 2)
            return;
        transform = transformFor(area);
        frames = buildFrames(area, transform);
    }

    m_visibleOrder.clear();
    m_visibleOrder.reserve(frames.size());
    for (const SeriesFrame &frame : frames)
        m_visibleOrder.append(frame.index);

    // Общее для всех режимов — рамка, сетка и подписи — рисуется один раз; режим
    // отвечает только за метки внутри поля.
    switch (m_view->mode()) {
    case PlotViewState::Mode::Xy:
        // У фазового портрета по обеим осям значения, поэтому ни временная сетка, ни
        // перекрестие по времени тут не к месту.
        painter.setPen(m_host->color(IPanelHost::ColorRole::Border));
        painter.drawRect(area);
        drawXy(painter, area);
        break;

    case PlotViewState::Mode::MultiPlot:
        drawMultiPlot(painter, area, transform);
        break;

    case PlotViewState::Mode::TimeSeries:
    case PlotViewState::Mode::Cumulative:
        // Накопление — та же развёртка, только прореживатель считает бегущую сумму;
        // осью может быть и принятый штамп, и монотонный счётчик от устройства.
        drawFrame(painter, area, transform, frames);
        drawTimeSeries(painter, area, transform, frames);
        drawCursor(painter, area, transform, frames);
        break;

    case PlotViewState::Mode::Histogram:
        // По горизонтали — значения, а не время: временна́я сетка и перекрестие тут ни при
        // чём, поэтому рисуется только рамка.
        painter.setPen(m_host->color(IPanelHost::ColorRole::Border));
        painter.drawRect(area);
        drawHistogram(painter, area);
        break;

    case PlotViewState::Mode::Spectrum:
        painter.setPen(m_host->color(IPanelHost::ColorRole::Border));
        painter.drawRect(area);
        drawSpectrum(painter, area);
        break;
    }

}

} // namespace spotty
