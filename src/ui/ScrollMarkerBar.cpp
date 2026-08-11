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

void ScrollMarkerBar::setMarkers(QList<Marker> markers, qint64 totalRows)
{
    // Сравнение дешевле перерисовки: терминал пересчитывает метки на каждой новой строке,
    // а полоса лежит поверх всего окна. Знаменатель входит в сравнение наравне с метками:
    // при неизменном наборе, но выросшем буфере они обязаны сдвинуться вверх.
    if (markers.size() == m_markers.size() && totalRows == m_totalRows) {
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
    m_totalRows = totalRows;
    update();
}

void ScrollMarkerBar::paintEvent(QPaintEvent *)
{
    QStyleOptionSlider option;
    initStyleOption(&option);

    QPainter painter(this);

    // QScrollBar рисует дорожку и бегунок одним сложным элементом. Если вызвать его
    // целиком, метки неизбежно окажутся поверх бегунка. Делим отрисовку на слои через
    // subControls: непрозрачная дорожка полностью очищает старый кадр, затем идут метки,
    // и только после них штатный полупрозрачный бегунок. Так под ним видно метку, но
    // сам бегунок остаётся верхним интерактивным слоем без шлейфа при перемещении.
    option.subControls = QStyle::SC_ScrollBarAddLine | QStyle::SC_ScrollBarSubLine
                         | QStyle::SC_ScrollBarAddPage | QStyle::SC_ScrollBarSubPage
                         | QStyle::SC_ScrollBarGroove;
    style()->drawComplexControl(QStyle::CC_ScrollBar, &option, &painter, this);

    if (!m_markers.isEmpty() && m_totalRows > 0) {
        // Дорожку меряем через стиль, а не по всей высоте виджета: на системах, где полоса
        // рисует кнопки со стрелками, метки иначе уехали бы под них.
        const QRect groove = style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                                     QStyle::SC_ScrollBarGroove, this);
        if (groove.height() > kMarkerHeight) {
            const int usable = groove.height() - kMarkerHeight;
            for (const Marker &marker : std::as_const(m_markers)) {
                const int offset = int(qBound(qint64(0), qint64(marker.position), m_totalRows - 1)
                                       * usable / m_totalRows);
                painter.fillRect(groove.left() + kMarkerInset, groove.top() + offset,
                                 groove.width() - 2 * kMarkerInset, kMarkerHeight, marker.color);
            }
        }
    }

    option.subControls = QStyle::SC_ScrollBarSlider;
    style()->drawComplexControl(QStyle::CC_ScrollBar, &option, &painter, this);
}

} // namespace spotty
