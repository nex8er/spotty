/**
 * \file PlotCanvas.cpp
 * \brief Реализация spotty::PlotCanvas.
 */
#include "PlotCanvas.h"

#include <spotty/data/PlotFormat.h>
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

namespace spotty {

namespace {

/// \brief Поля вокруг поля графика: слева под подписи значений, снизу под ось времени.
constexpr int kMarginLeft = 56;
constexpr int kMarginRight = 10;
constexpr int kMarginTop = 10;
constexpr int kMarginBottom = 22;

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

/// \brief Ширина цветной вкладки оси у левого края поля, px.
constexpr int kAxisTabWidth = 5;

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

    connect(m_model, &PlotModel::changed, this, &PlotCanvas::scheduleRepaint);
    // Состояние вида общее на все три холста, поэтому сдвиг окна в одном перерисовывает
    // остальные — это и означает «единый объект».
    connect(m_view, &PlotViewState::changed, this, &PlotCanvas::scheduleRepaint);

    createActions();
}

void PlotCanvas::scheduleRepaint()
{
    if (m_view->paused())
        return;

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
    pauseAction->setToolTip(tr("Freezes the picture, not the data: collecting continues."));
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
            m_view->setActiveSeries(series);
            event->accept();
            return;
        }
    }

    if (event->button() != Qt::LeftButton || !plotArea().contains(event->pos())) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_dragging = true;
    m_dragOrigin = event->pos();
    m_dragFrom = m_view->windowFrom();
    // Слежение снимается один раз, на нажатие. Снимать его на каждое движение значило бы
    // спорить с clampTo(), который у правого края включает его обратно: две стороны
    // переключали бы флаг в одном кадре, и график дрожал бы у края.
    m_view->setFollowing(false);
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void PlotCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PlotCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QRect area = plotArea();

    if (m_dragging && area.width() > 0) {
        // Сдвиг считается от точки нажатия, а не от прошлого события: так перетаскивание
        // не накапливает ошибку округления и график не уползает при возврате мыши назад.
        const int delta = event->pos().x() - m_dragOrigin.x();
        const qint64 duration = m_view->windowDuration();
        const qint64 shift = qint64(-double(delta) / double(area.width()) * double(duration));
        m_view->setWindow(m_dragFrom + shift, m_dragFrom + shift + duration);
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
    }

    event->accept();
}

void PlotCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    // Готовые длительности вместо числового поля: масштаб подбирают глазами, а «последние
    // десять секунд» — это то, что спрашивают у графика чаще всего.
    const QList<QPair<QString, qint64>> spans = {
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
        m_view->setFollowing(false);
        m_view->setWindow(samples.timestamp(0),
                          samples.timestamp(samples.sampleCount() - 1));
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
    return rect().adjusted(kMarginLeft, kMarginTop, -kMarginRight, -kMarginBottom);
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

XTransform PlotCanvas::transformFor(const QRect &area)
{
    const SampleBuffer &samples = m_model->samples();

    if (samples.sampleCount() >= 2) {
        m_view->clampTo(samples.timestamp(0),
                        samples.timestamp(samples.sampleCount() - 1));
    }

    return XTransform{m_view->windowFrom(), m_view->windowTo(), double(area.left()),
                      double(area.width())};
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

    for (int index = 0; index < m_model->seriesCount(); ++index) {
        if (!m_model->series(index).visible || index == m_model->xAxisSeries())
            continue;

        SeriesFrame frame;
        frame.index = index;
        frame.reduced =
            Decimator::reduce(m_model->samples(), index, transform, area.width(), accumulator);
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

void PlotCanvas::drawFrame(QPainter &painter, const QRect &area, const XTransform &transform,
                           const QList<SeriesFrame> &frames) const
{
    const QColor grid = m_host->color(IPanelHost::ColorRole::Border);
    const QColor muted = m_host->color(IPanelHost::ColorRole::TextMuted);

    painter.setPen(grid);
    painter.drawRect(area);

    const QFontMetrics metrics(font());

    // Слева подписана шкала активного ряда — того, что нарисован ярче остальных. Подписать
    // «какую-нибудь» значило бы дать числа, не относящиеся ни к одной видимой кривой.
    const int labelled = labelledSeries(frames);
    YScale scale;
    QColor labelColor = muted;
    for (const SeriesFrame &frame : frames) {
        if (frame.index != labelled)
            continue;
        scale = frame.scale;
        if (m_model->seriesCount() > 1)
            labelColor = QColor::fromRgba(m_model->series(frame.index).color);
    }

    for (int i = 0; i <= kGridLines + 1; ++i) {
        const double fraction = double(i) / double(kGridLines + 1);
        const int y = area.bottom() - int(fraction * area.height());

        if (i > 0 && i <= kGridLines) {
            painter.setPen(grid);
            painter.drawLine(area.left() + 1, y, area.right() - 1, y);
        }

        if (labelled < 0)
            continue;

        const double value = scale.minimum + fraction * (scale.maximum - scale.minimum);
        painter.setPen(labelColor);
        const QString label = PlotFormat::number(value, 4);
        painter.drawText(area.left() - metrics.horizontalAdvance(label) - 6,
                         y + metrics.ascent() / 2, label);
    }

    // Ось X — время окна, а не число отсчётов: точки приходят неравномерно, и «сколько
    // прошло секунд» отвечает на вопрос, который к графику и задают.
    painter.setPen(muted);
    const double seconds = double(transform.to - transform.from) / 1e9;
    painter.drawText(area.left(), area.bottom() + metrics.height(), QStringLiteral("0 s"));
    const QString right = QStringLiteral("%1 s").arg(seconds, 0, 'f', seconds < 10 ? 2 : 1);
    painter.drawText(area.right() - metrics.horizontalAdvance(right),
                     area.bottom() + metrics.height(), right);

    // Вкладки осей: по одной на видимый ряд, у самого края поля. Щелчок по вкладке делает
    // ряд активным, то есть переносит на него подписи слева, — это и есть «нажать на ось».
    if (frames.size() > 1) {
        painter.setPen(Qt::NoPen);
        for (int position = 0; position < frames.size(); ++position) {
            const SeriesFrame &frame = frames.at(position);
            QColor colour = QColor::fromRgba(m_model->series(frame.index).color);
            if (frame.index != labelled)
                colour.setAlpha(kDimmedAlpha);
            painter.setBrush(colour);
            painter.drawRect(seriesTabRect(position, int(frames.size()), area));
        }
        painter.setBrush(Qt::NoBrush);
    }
}

void PlotCanvas::drawCurve(QPainter &painter, const QRect &area, const SeriesFrame &frame,
                           const QColor &colour, bool withFill) const
{
    if (frame.reduced.columns.isEmpty())
        return;

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
        if (polyline.size() < 2)
            continue;

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
    // Фазовый портрет: время не участвует вовсе, по обеим осям идут значения. Колонка для
    // X берётся из того же выбора, что и в развёртке; без неё показывать нечего.
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
    const XTransform horizontal{0, 1, double(area.left()), double(area.width())};
    Q_UNUSED(horizontal);

    const auto xOf = [&](double value) {
        return area.left()
               + (value - xRange.minimum) / (xRange.maximum - xRange.minimum) * area.width();
    };

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
    const int bandHeight = area.height() / int(visible.size());

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
        frame.reduced = Decimator::reduce(m_model->samples(), index, transform, band.width());
        frame.scale = applyVertical(PlotScales::resolve(m_model->series(index),
                                                        frame.reduced.visible, {}),
                                    band);

        const QColor colour = QColor::fromRgba(m_model->series(index).color);
        drawCurve(painter, band, frame, colour, true);

        // Подпись полосы прямо в ней: отдельной шкалы слева на каждую полосу не хватит
        // места, а без имени полосы не различить.
        painter.setPen(colour);
        painter.drawText(band.adjusted(4, 2, -4, 0), Qt::AlignLeft | Qt::AlignTop,
                         m_model->series(index).name);

        if (position + 1 < visible.size()) {
            painter.setPen(grid);
            painter.drawLine(band.left(), band.bottom() + 1, band.right(), band.bottom() + 1);
        }
    }
}

void PlotCanvas::drawCursor(QPainter &painter, const QRect &area, const XTransform &transform,
                            const QList<SeriesFrame> &frames) const
{
    if (m_cursorX < 0 || frames.isEmpty())
        return;

    const SampleBuffer &samples = m_model->samples();
    if (samples.sampleCount() < 1)
        return;

    // Отсчёт под курсором ищется по времени, а не по доле длины ряда: счётчик отсчётов
    // один на все колонки, и перекрестие показывает именно то, что нарисовано.
    const qint64 time = transform.timeAt(m_cursorX);
    int row = Decimator::lowerBound(samples, time);
    row = qBound(0, row, samples.sampleCount() - 1);
    if (row > 0 && qAbs(samples.timestamp(row - 1) - time) < qAbs(samples.timestamp(row) - time))
        --row;

    const int x = int(transform.xOf(samples.timestamp(row)));
    if (x < area.left() || x > area.right())
        return;

    painter.setPen(QPen(m_host->color(IPanelHost::ColorRole::TextMuted), 1, Qt::DashLine));
    painter.drawLine(x, area.top(), x, area.bottom());

    const QFontMetrics metrics(font());
    int textY = area.top() + metrics.height();

    for (const SeriesFrame &frame : frames) {
        const double value = samples.at(row, frame.index);
        if (!qIsFinite(value))
            continue;

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
}

void PlotCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    // Сглаживание выключено намеренно и безусловно — см. замер в описании класса. Сетка и
    // рамка от этого не страдают: они из горизонтальных и вертикальных отрезков. Текст
    // сглаживается отдельным QPainter::TextAntialiasing, он здесь не трогается.
    QPainter painter(this);
    painter.fillRect(rect(), m_host->color(IPanelHost::ColorRole::Base));

    const QRect area = plotArea();
    if (area.width() < 2 || area.height() < 2)
        return;

    if (m_model->seriesCount() == 0) {
        painter.setPen(m_host->color(IPanelHost::ColorRole::TextMuted));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Waiting for numeric lines in the output"));
        return;
    }

    const XTransform transform = transformFor(area);
    const QList<SeriesFrame> frames = buildFrames(area, transform);

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
        // отдельного кода отрисовки ему не нужно.
        drawFrame(painter, area, transform, frames);
        drawTimeSeries(painter, area, transform, frames);
        drawCursor(painter, area, transform, frames);
        break;

    case PlotViewState::Mode::Histogram:
    case PlotViewState::Mode::Spectrum:
        // Появятся следующим шагом; пока показываем развёртку, а не пустое поле.
        drawFrame(painter, area, transform, frames);
        drawTimeSeries(painter, area, transform, frames);
        drawCursor(painter, area, transform, frames);
        break;
    }

    if (m_view->paused()) {
        // Замороженную картинку легко принять за зависшую программу. Надпись отвечает на
        // этот вопрос раньше, чем он возникнет.
        painter.setPen(m_host->color(IPanelHost::ColorRole::Accent));
        painter.drawText(area.adjusted(6, 4, -6, 0), Qt::AlignRight | Qt::AlignTop,
                         tr("PAUSED"));
    }
}

} // namespace spotty
