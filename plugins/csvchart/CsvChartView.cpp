/**
 * \file CsvChartView.cpp
 * \brief Реализация spotty::CsvChartView.
 */
#include "CsvChartView.h"

#include "CsvSeries.h"

#include <spotty/ui/IPanelHost.h>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace spotty {

namespace {

/// \brief Поля вокруг поля графика: слева под подписи значений, снизу под ось времени.
constexpr int kMarginLeft = 56;
constexpr int kMarginRight = 10;
constexpr int kMarginTop = 10;
constexpr int kMarginBottom = 22;

/// \brief Прозрачность заливки под линией.
constexpr int kFillAlpha = 36;

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

    connect(m_series, &CsvSeries::changed, this, [this] {
        if (!m_paused)
            update();
    });
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

void CsvChartView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
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

    drawFrame(painter, area, minimum, maximum);

    for (int s = 0; s < count; ++s) {
        const CsvSeries::Series &series = m_series->series(s);
        if (!series.visible || s == m_series->xAxisSeries() || series.values.size() < 2)
            continue;

        const double step = double(area.width()) / double(series.values.size() - 1);

        QPainterPath path;
        for (int i = 0; i < series.values.size(); ++i) {
            const double normalized = (series.values.at(i) - minimum) / span;
            const QPointF point(area.left() + step * i,
                                area.bottom() - normalized * area.height());
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }

        QPainterPath filled = path;
        filled.lineTo(area.right(), area.bottom());
        filled.lineTo(area.left(), area.bottom());
        filled.closeSubpath();
        QColor fill = series.color;
        fill.setAlpha(kFillAlpha);
        painter.fillPath(filled, fill);

        painter.setPen(QPen(series.color, 1.5));
        painter.drawPath(path);
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
