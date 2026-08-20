/**
 * \file SeriesHeaderView.cpp
 * \brief Реализация spotty::SeriesHeaderView.
 */
#include "SeriesHeaderView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>

namespace spotty {

namespace {

/// \brief Сторона квадратика, если стиль почему-то не назвал свою. Как в SeriesSwatchDelegate.
constexpr int kFallbackSide = 18;

/// \brief Скругление квадратика, долей стороны. Как в SeriesSwatchDelegate.
constexpr double kRadiusFraction = 0.2;

/**
 * \brief Цвет флажка заголовка.
 *
 * Нейтрально-серый, тот же, что у стрелок счётчика в `resources/icons/chevron-*.svg`:
 * проходит по контрасту и на тёмном фоне, и на белом, поэтому второго файла на вторую тему
 * не нужно. Не акцентный: этот флажок не принадлежит ни одному ряду, и цвет ряда на нём
 * читался бы как чужой. Не приглушённый цвет подписи: галочка белая, и на светлой теме она
 * бы на нём пропала.
 */
constexpr QColor indicatorColor() { return QColor(0x7a, 0x7f, 0x85); }

} // namespace

SeriesHeaderView::SeriesHeaderView(int column, QWidget *parent)
    : QHeaderView(Qt::Horizontal, parent)
    , m_column(column)
{
    // Заголовок, отданный таблице, по умолчанию не показывает своих секций.
    setSectionsClickable(true);
}

void SeriesHeaderView::setChecked(bool checked)
{
    if (m_checked == checked)
        return;
    m_checked = checked;
    updateSection(m_column);
}

QRect SeriesHeaderView::indicatorRect(const QRect &section) const
{
    // Та же пара «сторона от стиля, центр прямоугольника», что и в SeriesSwatchDelegate.
    // Здесь и там прямоугольники стоят в одной колонке, поэтому квадратики оказываются на
    // одной вертикали без подгонки.
    const int side = style() ? style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, this)
                             : 0;
    const int width = side > 0 ? side : kFallbackSide;
    return QRect(section.center().x() - width / 2, section.center().y() - width / 2, width,
                 width);
}

void SeriesHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    QHeaderView::paintSection(painter, rect, logicalIndex);
    if (logicalIndex != m_column)
        return;

    const QRect box = indicatorRect(rect);
    const double side = box.width();
    const double radius = side * kRadiusFraction;
    const QColor colour = indicatorColor();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // Снятый — пустой квадратик с рамкой, поставленный — залитый с галочкой. Ровно как у
    // рядов: состояние передано формой, а не только цветом.
    painter->setPen(QPen(colour, m_checked ? 1.0 : 2.0));
    painter->setBrush(m_checked ? QBrush(colour) : QBrush(Qt::NoBrush));
    painter->drawRoundedRect(QRectF(box).adjusted(1.0, 1.0, -1.0, -1.0), radius, radius);

    if (m_checked) {
        // Галочка путём, а не глифом: подходящий символ зависит от системы, а ломаная из
        // трёх точек одинакова везде. Пропорции те же, что у квадратиков рядов.
        QPainterPath check;
        check.moveTo(box.left() + side * 0.25, box.top() + side * 0.52);
        check.lineTo(box.left() + side * 0.43, box.top() + side * 0.71);
        check.lineTo(box.left() + side * 0.76, box.top() + side * 0.29);

        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(Qt::white, qMax(2.0, side / 7.5), Qt::SolidLine, Qt::RoundCap,
                             Qt::RoundJoin));
        painter->drawPath(check);
    }

    painter->restore();
}

void SeriesHeaderView::mousePressEvent(QMouseEvent *event)
{
    const QPoint position = event->position().toPoint();
    const int section = logicalIndexAt(position);

    // Только попадание в сам квадратик, а не во всю секцию: остальная её площадь остаётся
    // за базовым классом, которому там принадлежат захваты изменения ширины.
    if (event->button() == Qt::LeftButton && section == m_column) {
        const QRect box = indicatorRect(
            QRect(sectionViewportPosition(m_column), 0, sectionSize(m_column), height()));
        if (box.contains(position)) {
            setChecked(!m_checked);
            if (onToggled)
                onToggled(m_checked);
            event->accept();
            return;
        }
    }

    QHeaderView::mousePressEvent(event);
}

} // namespace spotty
