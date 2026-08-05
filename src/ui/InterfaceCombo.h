/**
 * \file InterfaceCombo.h
 * \brief Комбобокс выбора интерфейса со своей отрисовкой закрытого состояния.
 */
#pragma once

#include <QComboBox>

namespace spotty {

/**
 * \class InterfaceCombo
 * \brief QComboBox, показывающий служебные сведения не только в раскрытом списке.
 *
 * spotty::InterfaceItemDelegate раскрашивает раскрывшийся список, но Qt рисует закрытый
 * комбобокс отдельно, в обход делегата item-view — своим текстом из currentText(). Без
 * переопределения paintEvent() выбранный интерфейс, как только список закрывается, терял бы
 * системный адрес и время обнаружения — ровно то, ради чего эти сведения вообще показывают.
 */
class InterfaceCombo : public QComboBox
{
    Q_OBJECT

public:
    using QComboBox::QComboBox;

protected:
    void paintEvent(QPaintEvent *event) override;
};

} // namespace spotty
