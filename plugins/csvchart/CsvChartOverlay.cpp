/**
 * \file CsvChartOverlay.cpp
 * \brief Реализация spotty::CsvChartOverlay.
 */
#include "CsvChartOverlay.h"

#include "CsvSeries.h"

#include <spotty/ui/IPanelHost.h>

#include <QPainter>
#include <QPainterPath>

namespace spotty {

namespace {

/// \brief Поля вокруг графика внутри слоя.
constexpr int kMargin = 12;

/// \brief Прозрачность заливки под линией, 0…255.
constexpr int kFillAlpha = 40;

/**
 * \brief Цвета рядов.
 *
 * Своя палитра, а не роли темы: «цвет второго ряда» — не роль интерфейса, и тема о нём
 * знать не должна. У темы плагин спрашивает только светлая она или тёмная.
 */
const QColor kDarkSeries[] = {
    QColor(0x6C, 0xB6, 0xFF), QColor(0x8D, 0xDB, 0x8C), QColor(0xE8, 0xC4, 0x6A),
    QColor(0xE0, 0x8A, 0x8A), QColor(0xC0, 0x9C, 0xE8), QColor(0x7A, 0xD6, 0xD6),
};
const QColor kLightSeries[] = {
    QColor(0x1F, 0x6F, 0xC4), QColor(0x2E, 0x8B, 0x3D), QColor(0xB0, 0x7D, 0x11),
    QColor(0xC0, 0x39, 0x39), QColor(0x74, 0x4C, 0xC0), QColor(0x18, 0x8A, 0x8A),
};

} // namespace

CsvChartOverlay::CsvChartOverlay(IPanelHost *host, CsvSeries *series, QWidget *parent)
    : PanelWidget(host, parent)
    , m_series(series)
{
    setObjectName(QStringLiteral("csvChart"));
    setAttribute(Qt::WA_NoSystemBackground);

    connect(m_series, &CsvSeries::changed, this, qOverload<>(&QWidget::update));
}

QColor CsvChartOverlay::seriesColor(int index) const
{
    const QColor *palette = host()->isDarkTheme() ? kDarkSeries : kLightSeries;
    constexpr int count = int(std::size(kDarkSeries));
    return palette[index % count];
}

void CsvChartOverlay::themeChanged()
{
    update();
}

void CsvChartOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const int series = m_series->seriesCount();
    if (series == 0)
        return;

    const QRect area = rect().adjusted(kMargin, kMargin, -kMargin, -kMargin);
    if (area.width() < 2 || area.height() < 2)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    double minimum = 0.0;
    double maximum = 1.0;
    m_series->range(&minimum, &maximum);
    const double span = maximum - minimum;

    // Рамка и подписи пределов — без них цифры на графике не прочитать, а «красивая
    // кривая без осей» ничего не говорит о значениях.
    const QColor grid = host()->color(IPanelHost::ColorRole::Border);
    painter.setPen(grid);
    painter.drawRect(area);

    painter.setPen(host()->color(IPanelHost::ColorRole::TextMuted));
    painter.drawText(area.adjusted(4, 2, -4, 0), Qt::AlignLeft | Qt::AlignTop,
                     QString::number(maximum, 'g', 4));
    painter.drawText(area.adjusted(4, 0, -4, -2), Qt::AlignLeft | Qt::AlignBottom,
                     QString::number(minimum, 'g', 4));

    for (int s = 0; s < series; ++s) {
        const QList<double> &values = m_series->values(s);
        if (values.size() < 2)
            continue;

        const double step = double(area.width()) / double(values.size() - 1);

        QPainterPath path;
        for (int i = 0; i < values.size(); ++i) {
            const double normalized = (values.at(i) - minimum) / span;
            const QPointF point(area.left() + step * i,
                                area.bottom() - normalized * area.height());
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }

        const QColor color = seriesColor(s);

        // Заливка под линией с большой прозрачностью: она подсказывает, какая линия чья,
        // и не мешает читать текст терминала под графиком.
        QPainterPath filled = path;
        filled.lineTo(area.right(), area.bottom());
        filled.lineTo(area.left(), area.bottom());
        filled.closeSubpath();
        QColor fill = color;
        fill.setAlpha(kFillAlpha);
        painter.fillPath(filled, fill);

        painter.setPen(QPen(color, 1.5));
        painter.drawPath(path);
    }
}

} // namespace spotty
