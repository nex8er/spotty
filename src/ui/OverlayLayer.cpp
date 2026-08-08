/**
 * \file OverlayLayer.cpp
 * \brief Реализация spotty::OverlayLayer.
 */
#include "OverlayLayer.h"

#include "TerminalView.h"

#include <QEvent>
#include <QResizeEvent>

namespace spotty {

namespace {

/// \brief Отступ прижатой панели от края области вывода.
constexpr int kMargin = 8;

} // namespace

OverlayLayer::OverlayLayer(TerminalView *terminal)
    : QWidget(terminal ? terminal->viewport() : nullptr)
{
    setObjectName(QStringLiteral("panelOverlay"));

    // Ни фона, ни собственной отрисовки: под слоем должен остаться виден терминал.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    if (QWidget *viewport = parentWidget()) {
        viewport->installEventFilter(this);
        setGeometry(viewport->rect());
    }

    // Слой сам по себе пуст; показывать его до появления первой панели незачем, но и
    // прятать не нужно — пустой прозрачный виджет ничего не стоит и ничего не закрывает.
    raise();
}

void OverlayLayer::addOverlay(QWidget *widget, PanelAnchor anchor, bool mouseTransparent)
{
    if (!widget)
        return;

    widget->setParent(this);
    // Атрибут прозрачности для мыши на детей не наследуется, поэтому панель по умолчанию
    // свои клики получает. Отказаться — её собственное решение.
    widget->setAttribute(Qt::WA_TransparentForMouseEvents, mouseTransparent);
    widget->show();

    m_items.append({widget, anchor});
    layoutOverlays();
    raise();
}

bool OverlayLayer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        // raise() при каждом изменении размера: терминал волен создавать свои дочерние
        // виджеты позже нас, и тогда они окажутся выше по порядку укладки.
        raise();
    }
    return QWidget::eventFilter(watched, event);
}

void OverlayLayer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutOverlays();
}

void OverlayLayer::layoutOverlays()
{
    const QRect area = rect();
    if (area.isEmpty())
        return;

    for (const Item &item : std::as_const(m_items)) {
        if (item.anchor == PanelAnchor::Fill) {
            item.widget->setGeometry(area);
            continue;
        }

        // Прижатая панель занимает столько, сколько просит, но не больше доступного за
        // вычетом отступов: иначе она вылезет за край при узком окне.
        const QSize hint = item.widget->sizeHint();
        const int w = qMin(hint.width(), area.width() - 2 * kMargin);
        const int h = qMin(hint.height(), area.height() - 2 * kMargin);

        const bool right = item.anchor == PanelAnchor::TopRight
            || item.anchor == PanelAnchor::BottomRight;
        const bool bottom = item.anchor == PanelAnchor::BottomLeft
            || item.anchor == PanelAnchor::BottomRight;

        const int x = right ? area.right() - kMargin - w + 1 : area.left() + kMargin;
        const int y = bottom ? area.bottom() - kMargin - h + 1 : area.top() + kMargin;

        item.widget->setGeometry(x, y, w, h);
    }
}

} // namespace spotty
