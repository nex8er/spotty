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

    // Правый край — не по SC_ComboBoxEditField: QSS резервирует под стрелку 24px
    // (spotty.qss, правило QComboBox), а сама стрелка уже влезает в 22px из них — остаток
    // до края поля читался бы как пустая полоса перед служебными сведениями. Правый край
    // берётся напрямую от SC_ComboBoxArrow, вплотную к стрелке.
    const QRect arrowRect =
        style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, this);
    QRect rect =
        style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
    rect.setRight(arrowRect.left() - 2);
    rect.adjust(2, 0, 0, 0);

    const ThemeColors colors =
        ThemeManager::instance() ? ThemeManager::instance()->colors() : ThemeColors{};

    // Тот же канал приглушённого цвета для недоступных устройств, что и в делегате списка.
    const QVariant foreground = currentData(Qt::ForegroundRole);
    const QColor primaryColor = foreground.canConvert<QColor>() ? foreground.value<QColor>()
                                                                 : palette().color(QPalette::Text);

    painter.setFont(font());

    paintInterfaceRow(&painter, rect, currentText(),
                      currentData(InterfaceSecondaryTextRole).toString(), primaryColor,
                      colors.textMuted, font());
}

} // namespace spotty
