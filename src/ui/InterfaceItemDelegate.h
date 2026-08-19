/**
 * \file InterfaceItemDelegate.h
 * \brief Отрисовка пункта списка интерфейсов: своя раскладка вместо одной строки QComboBox.
 */
#pragma once

#include <QColor>
#include <QFont>
#include <QStyledItemDelegate>

namespace spotty {

/// \brief Роли данных, которые InterfaceBar кладёт в модель комбобокса для этого делегата.
enum InterfaceItemRole {
    InterfaceHeaderRole = Qt::UserRole + 1,        ///< bool — строка-заголовок группы плагина.
    InterfaceSecondaryTextRole = Qt::UserRole + 2, ///< QString — системный адрес и время обнаружения.
};

/**
 * \brief Нарисовать имя слева и служебные сведения приглушённым цветом справа.
 * \param painter Куда рисовать.
 * \param rect Прямоугольник строки, уже с нужными отступами по краям.
 * \param primary Основная подпись.
 * \param secondary Служебные сведения; пустая строка — не резервировать место справа.
 * \param primaryColor Цвет основной подписи.
 * \param secondaryColor Цвет служебных сведений.
 * \param font Шрифт для обеих частей.
 *
 * Общая для spotty::InterfaceItemDelegate (раскрывшийся список) и spotty::InterfaceCombo
 * (закрытый комбобокс) отрисовка — раскладка должна быть одна и та же, иначе выбранный
 * пункт выглядел бы по-разному до и после открытия списка. Служебные сведения прижаты
 * к правому краю rect максимально — вплоть до самого края поля, а не куда-то ближе к
 * имени.
 */
void paintInterfaceRow(QPainter *painter, const QRect &rect, const QString &primary,
                       const QString &secondary, const QColor &primaryColor,
                       const QColor &secondaryColor, const QFont &font);

/**
 * \class InterfaceItemDelegate
 * \brief Рисует пункт выпадающего списка интерфейсов вручную.
 *
 * QComboBox сам по себе показывает одну строку текста на пункт. Здесь нужны две зоны разной
 * значимости — основное имя слева, служебные сведения (системный адрес, время обнаружения)
 * приглушённым цветом справа — и невыбираемые строки-заголовки, группирующие устройства по
 * плагину-владельцу. То и другое требует собственной отрисовки.
 */
class InterfaceItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
              const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    /// \brief Отрисовать невыбираемую строку-заголовок группы.
    void paintHeader(QPainter *painter, const QStyleOptionViewItem &option,
                     const QModelIndex &index) const;
};

} // namespace spotty
