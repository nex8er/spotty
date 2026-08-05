/**
 * \file InterfaceCombo.cpp
 * \brief Реализация spotty::InterfaceCombo.
 */
#include "InterfaceCombo.h"

#include "InterfaceItemDelegate.h"
#include "theme/ThemeManager.h"

#include <QStyleOptionComboBox>
#include <QStylePainter>

namespace spotty {

void InterfaceCombo::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QStylePainter painter(this);

    QStyleOptionComboBox opt;
    initStyleOption(&opt);

    // Рамку и стрелку рисует стиль как обычно; текст очищаем и рисуем сами — иначе тут
    // был бы виден только currentText(), без системного адреса и времени обнаружения.
    opt.currentText.clear();
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);

    const QRect textRect =
        style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);

    const ThemeColors colors =
        ThemeManager::instance() ? ThemeManager::instance()->colors() : ThemeColors{};

    // Тот же канал приглушённого цвета для недоступных устройств, что и в делегате списка.
    const QVariant foreground = currentData(Qt::ForegroundRole);
    const QColor primaryColor = foreground.canConvert<QColor>() ? foreground.value<QColor>()
                                                                 : palette().color(QPalette::Text);

    paintInterfaceRow(&painter, textRect.adjusted(2, 0, -2, 0), currentText(),
                      currentData(InterfaceSecondaryTextRole).toString(), primaryColor,
                      colors.textMuted, font());
}

} // namespace spotty
