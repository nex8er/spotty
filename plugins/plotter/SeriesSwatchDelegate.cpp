/**
 * \file SeriesSwatchDelegate.cpp
 * \brief Реализация spotty::SeriesSwatchDelegate.
 */
#include "SeriesSwatchDelegate.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace spotty {

namespace {

/// \brief Сторона квадратика цвета, px.
constexpr int kSwatch = 15;

/// \brief Скругление квадратика, px.
constexpr double kRadius = 3.0;

/// \brief Прозрачность выключенного ряда: цвет виден, но ряд явно погашен.
constexpr int kHiddenAlpha = 60;

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

QSize SeriesSwatchDelegate::sizeHint(const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(kSwatch + 10, kSwatch + 6);
}

void SeriesSwatchDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    // Фон ячейки — за таблицей стилей: копия параметров без текста отдаётся базовому
    // классу, и выделение с наведением рисуются ровно так же, как в остальных колонках.
    QStyleOptionViewItem plain = option;
    initStyleOption(&plain, index);
    plain.text.clear();
    plain.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    QStyledItemDelegate::paint(painter, plain, index);

    const QColor colour = QColor::fromRgba(index.data(kColorRole).toUInt());
    const bool visible = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;

    const QRect box(option.rect.center().x() - kSwatch / 2,
                    option.rect.center().y() - kSwatch / 2, kSwatch, kSwatch);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QColor fill = colour;
    if (!visible) {
        // Выключенный ряд гасится прозрачностью, а не серым: цвет — единственное, чем он
        // связан со своей кривой, и терять эту связь при выключении нельзя.
        fill.setAlpha(kHiddenAlpha);
    }

    painter->setPen(QPen(colour.darker(140), 1));
    painter->setBrush(fill);
    painter->drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

    if (visible) {
        // Галочка рисуется путём, а не шрифтом: подходящий глиф зависит от системы, а
        // ломаная из трёх точек одинакова везде.
        QPainterPath check;
        check.moveTo(box.left() + kSwatch * 0.25, box.top() + kSwatch * 0.52);
        check.lineTo(box.left() + kSwatch * 0.43, box.top() + kSwatch * 0.71);
        check.lineTo(box.left() + kSwatch * 0.76, box.top() + kSwatch * 0.29);

        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(needsLightCheck(colour) ? Qt::white : Qt::black, 2.0,
                             Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
