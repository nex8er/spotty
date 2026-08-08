/**
 * \file OverlayLayer.h
 * \brief Стеклянная панель поверх области вывода терминала.
 */
#pragma once

#include <spotty/ui/PanelDescriptor.h>

#include <QList>
#include <QWidget>

namespace spotty {

class TerminalView;

/**
 * \class OverlayLayer
 * \brief Слой, на котором живут панели с размещением PanelPlacement::Overlay.
 *
 * \par Почему ребёнок области прокрутки, а не самого терминала
 *
 * spotty::TerminalView — это QAbstractScrollArea. Слой, положенный на него целиком,
 * закрыл бы полосы прокрутки. Ребёнок `viewport()` ложится ровно на область текста.
 *
 * Прокрутка слой не задевает: `TerminalView::scrollContentsBy()` перерисовывает viewport
 * и не сдвигает его детей — содержимое рисуется в paintEvent от позиции полосы, а не
 * перемещением виджетов. Слой поэтому остаётся на месте, как и должен.
 *
 * \par Прозрачность
 *
 * Фона у слоя нет (`WA_NoSystemBackground` и отсутствие `WA_OpaquePaintEvent`), поэтому
 * Qt рисует под ним viewport целиком, а дети накладываются со своей альфой —
 * полупрозрачность получается сама, без композитинга на уровне окна.
 *
 * Сам слой прозрачен для мыши (`WA_TransparentForMouseEvents`), иначе выделение текста в
 * терминале перестало бы работать. На детей этот атрибут не наследуется, поэтому панель
 * получает свои клики; отказаться от них она может флагом
 * PanelDescriptor::mouseTransparent.
 *
 * \warning Слою нужно правило `#panelOverlay { background: transparent; border: none; }`
 *          в таблице стилей. Без него он унаследует вид `#card`, и это будет выглядеть
 *          как «панель закрыла терминал», а не как ошибка оформления.
 */
class OverlayLayer : public QWidget
{
    Q_OBJECT

public:
    explicit OverlayLayer(TerminalView *terminal);

    /**
     * \brief Положить панель на слой.
     * \param widget Виджет панели; слой становится его родителем.
     * \param anchor Как прижать.
     * \param mouseTransparent Пропускать мышь насквозь к терминалу.
     */
    void addOverlay(QWidget *widget, PanelAnchor anchor, bool mouseTransparent);

protected:
    /// \brief Следит за изменением размера области прокрутки.
    bool eventFilter(QObject *watched, QEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

private:
    /// \brief Расставить панели по якорям.
    void layoutOverlays();

    /**
     * \struct Item
     * \brief Панель на слое вместе с тем, как её прижимать.
     */
    struct Item
    {
        QWidget *widget = nullptr;
        PanelAnchor anchor = PanelAnchor::Fill;
    };

    QList<Item> m_items;
};

} // namespace spotty
