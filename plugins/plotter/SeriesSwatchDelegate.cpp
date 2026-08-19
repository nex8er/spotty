/**
 * \file SeriesSwatchDelegate.cpp
 * \brief Реализация spotty::SeriesSwatchDelegate.
 */
#include "SeriesSwatchDelegate.h"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>

namespace spotty {

namespace {

/// \brief Сторона квадратика, если стиль почему-то не назвал свою.
constexpr int kFallbackSide = 18;

/// \brief Скругление квадратика, долей стороны.
constexpr double kRadiusFraction = 0.2;

/// \brief Отбивка вокруг квадратика внутри ячейки, px.
constexpr int kCellPadding = 6;

/**
 * \brief Достаточно ли цвет тёмен, чтобы галочка на нём была белой.
 *
 * Относительная яркость по коэффициентам восприятия, а не среднее по каналам: глаз
 * заметно чувствительнее к зелёному, и на жёлтом фоне среднее дало бы белую галочку,
 * которой не видно.
 */
bool needsLightCheck(const QColor &color)
{
    const double luminance =
        0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF();
    return luminance < 0.55;
}

} // namespace

int SeriesSwatchDelegate::swatchSide(const QStyleOptionViewItem &option)
{
    // Размер берётся у стиля, а не задаётся числом: квадратик стоит на месте штатного
    // флажка и обязан быть с него ростом, иначе строка выглядит съехавшей.
    const QStyle *style = option.widget ? option.widget->style() : QApplication::style();
    const int side = style ? style->pixelMetric(QStyle::PM_IndicatorWidth, &option,
                                                option.widget)
                           : 0;
    return side > 0 ? side : kFallbackSide;
}

QSize SeriesSwatchDelegate::sizeHint(const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    Q_UNUSED(index);
    const int side = swatchSide(option);
    return QSize(side + kCellPadding * 2, side + kCellPadding);
}

void SeriesSwatchDelegate::initStyleOption(QStyleOptionViewItem *option,
                                           const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    // Штатный флажок и текст рисует делегат сам, поэтому у стиля их забираем. Снять это
    // после initStyleOption() нельзя: базовый paint() зовёт его заново.
    option->features &= ~QStyleOptionViewItem::HasCheckIndicator;
    option->text.clear();
}

void SeriesSwatchDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    // Фон ячейки — за таблицей стилей: выделение и наведение рисуются ровно так же, как в
    // остальных колонках. Флажок и текст сняты в initStyleOption().
    QStyledItemDelegate::paint(painter, option, index);

    const QColor colour = QColor::fromRgba(index.data(kColorRole).toUInt());
    const bool visible = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;

    const int side = swatchSide(option);
    const double radius = side * kRadiusFraction;
    const QRect box(option.rect.center().x() - side / 2, option.rect.center().y() - side / 2,
                    side, side);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // Выключенный ряд — пустой квадратик с рамкой в полный цвет, включённый — залитый.
    // Гасить прозрачностью всю заливку нельзя: на тёмной теме приглушённый цвет сливается
    // с фоном, и выключенный ряд выглядит не выключенным, а ненарисованным. Рамка же
    // держит цвет в полную силу, и связь строки со своей кривой не теряется.
    painter->setPen(QPen(visible ? colour.darker(140) : colour, visible ? 1.0 : 2.0));
    painter->setBrush(visible ? QBrush(colour) : QBrush(Qt::NoBrush));
    painter->drawRoundedRect(QRectF(box).adjusted(1.0, 1.0, -1.0, -1.0), radius, radius);

    if (visible) {
        // Галочка рисуется путём, а не шрифтом: подходящий глиф зависит от системы, а
        // ломаная из трёх точек одинакова везде.
        QPainterPath check;
        check.moveTo(box.left() + side * 0.25, box.top() + side * 0.52);
        check.lineTo(box.left() + side * 0.43, box.top() + side * 0.71);
        check.lineTo(box.left() + side * 0.76, box.top() + side * 0.29);

        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(needsLightCheck(colour) ? Qt::white : Qt::black,
                             qMax(2.0, side / 7.5), Qt::SolidLine, Qt::RoundCap,
                             Qt::RoundJoin));
        painter->drawPath(check);
    }

    painter->restore();
}

bool SeriesSwatchDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                       const QStyleOptionViewItem &option,
                                       const QModelIndex &index)
{
    Q_UNUSED(model);
    Q_UNUSED(option);

    if (event->type() == QEvent::MouseButtonDblClick) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            Q_EMIT colourRequested(index.row());
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            Q_EMIT visibilityToggled(index.row());
            return true;
        }
    }

    return false;
}

} // namespace spotty
