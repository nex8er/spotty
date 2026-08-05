/**
 * \file InterfaceItemDelegate.cpp
 * \brief Реализация spotty::InterfaceItemDelegate.
 */
#include "InterfaceItemDelegate.h"

#include "theme/ThemeManager.h"

#include <QPainter>

namespace spotty {

namespace {

constexpr int kHorizontalMargin = 6; ///< Тот же отступ, что даёт QSS для ::item.

} // namespace

void paintInterfaceRow(QPainter *painter, const QRect &rect, const QString &primary,
                       const QString &secondary, const QColor &primaryColor,
                       const QColor &secondaryColor, const QFont &font)
{
    constexpr int kColumnGap = 10; ///< Промежуток между именем и служебными сведениями.

    painter->save();
    painter->setFont(font);

    const QFontMetrics metrics(font);
    const int secondaryWidth = secondary.isEmpty() ? 0 : metrics.horizontalAdvance(secondary);

    QRect primaryRect = rect;
    if (secondaryWidth > 0)
        primaryRect.setRight(rect.right() - secondaryWidth - kColumnGap);

    painter->setPen(primaryColor);
    painter->drawText(primaryRect, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(primary, Qt::ElideRight, primaryRect.width()));

    if (secondaryWidth > 0) {
        QRect secondaryRect = rect;
        secondaryRect.setLeft(primaryRect.right() + kColumnGap);
        painter->setPen(secondaryColor);
        painter->drawText(secondaryRect, Qt::AlignVCenter | Qt::AlignRight, secondary);
    }

    painter->restore();
}

void InterfaceItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
    if (index.data(InterfaceHeaderRole).toBool()) {
        paintHeader(painter, option, index);
        return;
    }

    const ThemeColors colors =
        ThemeManager::instance() ? ThemeManager::instance()->colors() : ThemeColors{};

    painter->save();
    painter->fillRect(option.rect, (option.state & QStyle::State_Selected) ? colors.selection
                                                                            : colors.base);
    painter->restore();

    // Приглушённый цвет недоступного устройства приходит через ForegroundRole — тот же
    // канал, которым rebuild() красит пункт и без делегата.
    const QVariant foreground = index.data(Qt::ForegroundRole);
    const QColor primaryColor =
        foreground.canConvert<QColor>() ? foreground.value<QColor>() : colors.text;

    paintInterfaceRow(painter, option.rect.adjusted(kHorizontalMargin, 0, -kHorizontalMargin, 0),
                      index.data(Qt::DisplayRole).toString(),
                      index.data(InterfaceSecondaryTextRole).toString(), primaryColor,
                      colors.textMuted, option.font);
}

void InterfaceItemDelegate::paintHeader(QPainter *painter, const QStyleOptionViewItem &option,
                                        const QModelIndex &index) const
{
    const ThemeColors colors =
        ThemeManager::instance() ? ThemeManager::instance()->colors() : ThemeColors{};

    painter->save();
    painter->fillRect(option.rect, colors.panel);

    QFont font = option.font;
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(colors.textMuted);
    painter->drawText(option.rect.adjusted(kHorizontalMargin, 0, -kHorizontalMargin, 0),
                      Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
    painter->restore();
}

QSize InterfaceItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    if (index.data(InterfaceHeaderRole).toBool())
        size.setHeight(size.height() + 6); // отступ отделяет группу от предыдущей
    return size;
}

} // namespace spotty
