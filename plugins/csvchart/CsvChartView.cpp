/**
 * \file CsvChartView.cpp
 * \brief Реализация spotty::CsvChartView.
 */
#include "CsvChartView.h"

#include "CsvSeries.h"

#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QAction>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QTimer>

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
 * Заливка подчёркивает форму, пока кривых одна-две. Десять полупрозрачных заливок,
 * положенных друг на друга, дают мутное пятно, сквозь которое не читается ни одна из них,
 * — и стоят при этом дороже всей остальной отрисовки вместе взятой (замер: 1.3 мс на линии
 * против 7.6 мс с заливками). Там, где она мешает, её и не рисуем.
 */
constexpr int kMaxFilledSeries = 3;

/// \brief Число горизонтальных линий сетки, не считая краёв.
constexpr int kGridLines = 3;

} // namespace

CsvChartView::CsvChartView(IPanelHost *host, CsvSeries *series, QWidget *parent)
    : QWidget(parent)
    , m_host(host)
    , m_series(series)
{
    setObjectName(QStringLiteral("csvChart"));
    // Перекрестие следует за курсором без нажатия: снимать значение зажатой кнопкой
    // неудобно, а другого смысла у нажатия здесь нет.
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

    connect(m_series, &CsvSeries::changed, this, &CsvChartView::scheduleRepaint);

    createActions();
}

void CsvChartView::scheduleRepaint()
{
    if (m_paused)
        return;

    m_dirty = true;
    if (!m_repaintTimer->isActive())
        m_repaintTimer->start();
}

void CsvChartView::createActions()
{
    m_pauseAction = new QAction(tr("Pause"), this);
    m_pauseAction->setCheckable(true);
    m_pauseAction->setIcon(m_host->icon(mdi::Pause, 18));
    m_pauseAction->setToolTip(tr("Freezes the picture, not the data: collecting continues."));
    connect(m_pauseAction, &QAction::toggled, this, &CsvChartView::setPaused);
    // Пауза переключается и двойным щелчком по полю — кнопка обязана это отразить.
    connect(this, &CsvChartView::pausedChanged, m_pauseAction, [this](bool paused) {
        const QSignalBlocker blocker(m_pauseAction);
        m_pauseAction->setChecked(paused);
    });
    addAction(m_pauseAction);

    auto *clearAction = new QAction(tr("Clear"), this);
    clearAction->setIcon(m_host->icon(mdi::Broom, 18));
    connect(clearAction, &QAction::triggered, m_series, &CsvSeries::clear);
    addAction(clearAction);
}

void CsvChartView::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    update();
}

void CsvChartView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Двойной щелчок по полю — самый быстрый способ заморозить картинку, когда нужное
    // мелькнуло и вот-вот уедет за край.
    setPaused(!m_paused);
    Q_EMIT pausedChanged(m_paused);
    event->accept();
}

void CsvChartView::mouseMoveEvent(QMouseEvent *event)
{
    const QRect area = plotArea();
    m_cursorX = area.contains(event->pos()) ? event->pos().x() : -1;
    update();
}

void CsvChartView::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_cursorX = -1;
    update();
}

QRect CsvChartView::plotArea() const
{
    return rect().adjusted(kMarginLeft, kMarginTop, -kMarginRight, -kMarginBottom);
}

bool CsvChartView::saveImage(const QString &filePath)
{
    // Снимок рисуется в отступе устройства, а не в логических точках: на экране Retina
    // сохранённый в точках график выходит вдвое мельче и выглядит размытым.
    QPixmap pixmap(size() * devicePixelRatioF());
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    pixmap.fill(m_host->color(IPanelHost::ColorRole::Base));
    render(&pixmap);
    return pixmap.save(filePath, "PNG");
}

void CsvChartView::drawFrame(QPainter &painter, const QRect &area, double minimum,
                             double maximum) const
{
    const QColor grid = m_host->color(IPanelHost::ColorRole::Border);
    const QColor muted = m_host->color(IPanelHost::ColorRole::TextMuted);

    painter.setPen(grid);
    painter.drawRect(area);

    painter.setPen(muted);
    const QFontMetrics metrics(font());

    // Горизонтальные линии с подписями. Без них по кривой нельзя снять значение, а
    // «красивая линия без осей» не отвечает ни на один вопрос.
    for (int i = 0; i <= kGridLines + 1; ++i) {
        const double fraction = double(i) / double(kGridLines + 1);
        const int y = area.bottom() - int(fraction * area.height());
        const double value = minimum + fraction * (maximum - minimum);

        if (i > 0 && i <= kGridLines) {
            painter.setPen(grid);
            painter.drawLine(area.left() + 1, y, area.right() - 1, y);
        }

        painter.setPen(muted);
        const QString label = QString::number(value, 'g', 4);
        painter.drawText(area.left() - metrics.horizontalAdvance(label) - 6,
                         y + metrics.ascent() / 2, label);
    }

    // Ось X: длительность накопленного окна. Номер отсчёта здесь бесполезен — точки
    // приходят неравномерно, и «сколько прошло секунд» отвечает на вопрос, который к
    // графику задают.
    const QList<qint64> &stamps = m_series->timestamps();
    if (stamps.size() >= 2) {
        const double seconds = double(stamps.last() - stamps.first()) / 1e9;
        painter.drawText(area.left(), area.bottom() + metrics.height(),
                         QStringLiteral("0 s"));
        const QString right = QStringLiteral("%1 s").arg(seconds, 0, 'f', 1);
        painter.drawText(area.right() - metrics.horizontalAdvance(right),
                         area.bottom() + metrics.height(), right);
    }
}

void CsvChartView::drawCursor(QPainter &painter, const QRect &area, double minimum,
                              double maximum) const
{
    if (m_cursorX < 0)
        return;

    const int count = m_series->seriesCount();
    if (count == 0)
        return;

    // Номер точки под курсором. Берём длину первого видимого ряда: ряды заполняются
    // синхронно, и расхождение возможно только на последнюю точку.
    int longest = 0;
    for (int s = 0; s < count; ++s)
        longest = qMax(longest, int(m_series->series(s).values.size()));
    if (longest < 2)
        return;

    const double fraction = double(m_cursorX - area.left()) / double(area.width());
    const int index = qBound(0, int(fraction * (longest - 1) + 0.5), longest - 1);
    const int x = area.left() + int(double(index) / double(longest - 1) * area.width());

    painter.setPen(QPen(m_host->color(IPanelHost::ColorRole::TextMuted), 1, Qt::DashLine));
    painter.drawLine(x, area.top(), x, area.bottom());

    // Значения всех видимых рядов в этой точке — ради них перекрестие и нужно.
    const QFontMetrics metrics(font());
    int textY = area.top() + metrics.height();
    for (int s = 0; s < count; ++s) {
        const CsvSeries::Series &series = m_series->series(s);
        if (!series.visible || index >= series.values.size())
            continue;

        const QString label = QStringLiteral("%1  %2")
                                  .arg(series.name)
                                  .arg(series.values.at(index), 0, 'g', 5);

        // Подпись уходит на ту сторону от курсора, где больше места: иначе у правого края
        // она обрезается ровно тогда, когда смотрят на свежие данные.
        const int width = metrics.horizontalAdvance(label);
        const int labelX = (x + 8 + width < area.right()) ? x + 8 : x - 8 - width;

        painter.setPen(series.color);
        painter.drawText(labelX, textY, label);
        textY += metrics.height();
    }

    Q_UNUSED(minimum);
    Q_UNUSED(maximum);
}

QPolygonF CsvChartView::seriesPolyline(const QList<double> &values, const QRect &area,
                                       double minimum, double span) const
{
    QPolygonF polyline;

    const int count = int(values.size());
    if (count < 2)
        return polyline;

    const double bottom = area.bottom();
    const double height = area.height();
    const auto yOf = [&](double value) {
        return bottom - (value - minimum) / span * height;
    };

    const int width = area.width();
    if (count <= width) {
        // Точек меньше, чем пикселей: прореживать нечего, и каждая из них видна отдельно.
        polyline.reserve(count);
        const double step = double(width) / double(count - 1);
        for (int i = 0; i < count; ++i)
            polyline.append(QPointF(area.left() + step * i, yOf(values.at(i))));
        return polyline;
    }

    // По паре точек на колонку пикселей. Каждое значение просматривается ровно один раз,
    // поэтому стоимость линейна по числу точек и не зависит от ширины поля.
    polyline.reserve(width * 2);
    for (int x = 0; x < width; ++x) {
        const int from = int(qint64(x) * count / width);
        const int to = qMax(from + 1, int(qint64(x + 1) * count / width));

        double lowest = values.at(from);
        double highest = lowest;
        for (int i = from + 1; i < to && i < count; ++i) {
            const double value = values.at(i);
            lowest = qMin(lowest, value);
            highest = qMax(highest, value);
        }

        // Сверху вниз в экранных координатах: порядок один и тот же во всех колонках,
        // поэтому соседние отрезки смыкаются, а не расходятся зигзагом.
        const double px = area.left() + x;
        polyline.append(QPointF(px, yOf(highest)));
        polyline.append(QPointF(px, yOf(lowest)));
    }

    return polyline;
}

void CsvChartView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    // Сглаживание выключено намеренно и безусловно.
    //
    // Оно и было той самой причиной, по которой окно переставало отвечать. Замер: одна и та
    // же кривая из 200 точек рисуется 2.9 мс, если она гладкая, и 676 мс, если дёрганая —
    // цена зависит не от числа точек, а от того, сколько пера ложится на экран. Поэтому
    // включать его «когда точек мало» нельзя: 200 точек — это умолчание окна, и на шумном
    // сигнале оно давало полтора кадра в секунду. Надёжного и дешёвого признака «здесь
    // сглаживание по карману» не нашлось, а разброс цены — тысячекратный.
    //
    // Сетка и рамка от этого не страдают: они из горизонтальных и вертикальных отрезков,
    // которым сглаживать нечего. Подписи тоже — за текст отвечает отдельный
    // QPainter::TextAntialiasing, он включён по умолчанию и здесь не трогается.
    QPainter painter(this);
    painter.fillRect(rect(), m_host->color(IPanelHost::ColorRole::Base));

    const QRect area = plotArea();
    if (area.width() < 2 || area.height() < 2)
        return;

    const int count = m_series->seriesCount();
    if (count == 0) {
        painter.setPen(m_host->color(IPanelHost::ColorRole::TextMuted));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Waiting for numeric lines in the output"));
        return;
    }

    double minimum = 0.0;
    double maximum = 1.0;
    m_series->range(&minimum, &maximum);
    const double span = maximum - minimum;

    int longest = 0;
    int visible = 0;
    for (int s = 0; s < count; ++s) {
        longest = qMax(longest, int(m_series->series(s).values.size()));
        if (m_series->series(s).visible && s != m_series->xAxisSeries())
            ++visible;
    }
    const bool dense = longest > area.width();
    const bool withFill = visible <= kMaxFilledSeries;

    drawFrame(painter, area, minimum, maximum);

    for (int s = 0; s < count; ++s) {
        const CsvSeries::Series &series = m_series->series(s);
        if (!series.visible || s == m_series->xAxisSeries() || series.values.size() < 2)
            continue;

        const QPolygonF polyline = seriesPolyline(series.values, area, minimum, span);
        if (polyline.size() < 2)
            continue;

        if (withFill) {
            // Заливается верхняя огибающая, а не сама полилиния. Прореженная полилиния —
            // «пила» из вертикальных отрезков, и как многоугольник она пересекает себя в
            // каждой колонке: заливка такой фигуры заставляет растеризатор разбирать тысячи
            // самопересечений на каждой строке развёртки. Огибающая монотонна по x, а
            // выглядит ровно так же — нижняя граница заливки всё равно у основания.
            QPolygonF filled;
            const bool decimated = int(series.values.size()) > area.width();
            if (decimated) {
                filled.reserve(polyline.size() / 2 + 2);
                for (qsizetype i = 0; i < polyline.size(); i += 2)
                    filled.append(polyline.at(i));
            } else {
                filled = polyline;
            }
            filled.append(QPointF(polyline.last().x(), area.bottom()));
            filled.append(QPointF(polyline.first().x(), area.bottom()));

            QColor fill = series.color;
            fill.setAlpha(kFillAlpha);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawPolygon(filled);
            painter.setBrush(Qt::NoBrush);
        }

        // Дробная толщина без сглаживания всё равно округлится до целой, и просить её
        // означало бы платить за подготовку пера впустую.
        painter.setPen(QPen(series.color, dense ? 1.0 : 1.5));
        painter.drawPolyline(polyline);
    }

    drawCursor(painter, area, minimum, maximum);

    if (m_paused) {
        // Замороженную картинку легко принять за зависшую программу. Надпись отвечает на
        // этот вопрос раньше, чем он возникнет.
        painter.setPen(m_host->color(IPanelHost::ColorRole::Accent));
        painter.drawText(area.adjusted(6, 4, -6, 0), Qt::AlignRight | Qt::AlignTop,
                         tr("PAUSED"));
    }
}

} // namespace spotty
