/**
 * \file ScrollMarkerBar.cpp
 * \brief Реализация spotty::ScrollMarkerBar.
 */
#include "ScrollMarkerBar.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>

namespace spotty {

namespace {

/// \brief Высота метки в пикселях. Меньше трёх — не видно, больше — метки слипаются.
constexpr int kMarkerHeight = 3;

/// \brief Отступ метки от краёв полосы: она не должна налезать на скругление ползунка.
constexpr int kMarkerInset = 2;

} // namespace

ScrollMarkerBar::ScrollMarkerBar(QWidget *parent)
    : QScrollBar(Qt::Vertical, parent)
{
}

void ScrollMarkerBar::setMarkers(QList<Marker> markers)
{
    // Сравнение дешевле перерисовки: терминал пересчитывает метки на каждой новой строке,
    // а полоса лежит поверх всего окна.
    if (markers.size() == m_markers.size()) {
        bool same = true;
        for (qsizetype i = 0; i < markers.size(); ++i) {
            if (markers.at(i).position != m_markers.at(i).position
                || markers.at(i).color != m_markers.at(i).color) {
                same = false;
                break;
            }
        }
        if (same)
            return;
    }

    m_markers = std::move(markers);
    update();
}

void ScrollMarkerBar::paintEvent(QPaintEvent *event)
{
    // Сначала штатная отрисовка: метки ложатся поверх дорожки, но под ползунком остаются
    // видны — он полупрозрачный.
    QScrollBar::paintEvent(event);

    if (m_markers.isEmpty())
        return;

    const int span = maximum() - minimum();
    if (span <= 0)
        return;

    // Дорожку меряем через стиль, а не по всей высоте виджета: на системах, где полоса
    // рисует кнопки со стрелками, метки иначе уехали бы под них.
    QStyleOptionSlider option;
    initStyleOption(&option);
    const QRect groove =
        style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarGroove, this);
    if (groove.height() <= kMarkerHeight)
        return;

    const int usable = groove.height() - kMarkerHeight;

    QPainter painter(this);
    for (const Marker &marker : std::as_const(m_markers)) {
        const int offset = int(qint64(marker.position - minimum()) * usable / span);
        painter.fillRect(groove.left() + kMarkerInset, groove.top() + offset,
                         groove.width() - 2 * kMarkerInset, kMarkerHeight, marker.color);
    }
}

} // namespace spotty
