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

/// \brief Непрозрачность меток: видны при поиске, но не спорят с бегунком.
constexpr int kMarkerOpacity = 85;

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

void ScrollMarkerBar::paintEvent(QPaintEvent *event)
{
    QStyleOptionSlider option;
    initStyleOption(&option);

    // Полная штатная отрисовка прежде всего очищает дорожку непрозрачным фоном. Это
    // обязательно для полупрозрачных элементов: ограниченная перерисовка оставила бы
    // след бегунка в его прежнем положении.
    QScrollBar::paintEvent(event);

    QPainter painter(this);

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
                // Дорожка уже полностью перерисована непрозрачным фоном, поэтому
                // полупрозрачная метка не оставляет следов при обновлении набора.
                QColor color = marker.color;
                color.setAlpha(kMarkerOpacity);
                painter.fillRect(groove.left() + kMarkerInset, groove.top() + offset,
                                 groove.width() - 2 * kMarkerInset, kMarkerHeight, color);
            }
        }
    }

    // QStyleSheetStyle не умеет надёжно отрисовывать только один subControl: повторный
    // drawComplexControl() снова закрашивает всю дорожку и скрывает метки. Бегунок уже
    // имеет собственный QSS-вид, поэтому повторяем лишь его заливку поверх меток. Цвет
    // берём из роли PlaceholderText — ThemeManager связывает её с textMuted, тем же
    // токеном, который использует @scrollHandle.
    const QRect slider =
        style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, this)
            .adjusted(2, 2, -2, -2);
    QColor handle = palette().color(QPalette::PlaceholderText);
    const bool hovered = option.activeSubControls == QStyle::SC_ScrollBarSlider
                         && option.state.testFlag(QStyle::State_MouseOver);
    handle.setAlpha(hovered || isSliderDown() ? 210 : 105);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(handle);
    painter.drawRoundedRect(slider, 2, 2);
}

} // namespace spotty
