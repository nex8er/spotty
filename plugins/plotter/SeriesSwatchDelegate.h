/**
 * \file SeriesSwatchDelegate.h
 * \brief Первая колонка таблицы рядов: галочка видимости прямо на цвете ряда.
 */
#pragma once

#include <QStyledItemDelegate>

namespace spotty {

/**
 * \class SeriesSwatchDelegate
 * \brief Рисует флажок видимости, залитый цветом ряда.
 *
 * \par Почему делегат, а не значок и не живой QCheckBox
 *
 * Значком (как было раньше) нельзя нарисовать галочку **на** цвете — только рядом, и тогда
 * колонок нужно две вместо одной. Настоящий QCheckBox в ячейке даёт по виджету на строку в
 * таблице, которая перестраивается при каждой новой колонке потока, и вдобавок попадает в
 * записанную ловушку: Fusion выводит обводку флажка из QPalette::Window, и правило QSS
 * обязано само подставить изображение галочки.
 *
 * \par Как обходится вторая записанная ловушка
 *
 * `setBackground()` у ячейки не работает, пока действует правило `QTableView::item` из
 * таблицы стилей. Делегат её и не трогает: он очищает текст у копии параметров, отдаёт
 * отрисовку фона базовому классу — выделение и наведение остаются за QSS, — и только потом
 * рисует поверх. Так стиль отвечает за фон, а делегат за содержимое, и спорить им не о чем.
 */
class SeriesSwatchDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    /// \brief Роль, из которой берётся цвет ряда (`0xAARRGGBB` числом).
    static constexpr int kColorRole = Qt::UserRole + 1;

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

Q_SIGNALS:
    /// \brief Одиночный щелчок по квадратику — переключить видимость ряда.
    void visibilityToggled(int row);

    /// \brief Двойной щелчок — сменить цвет ряда.
    void colourRequested(int row);

protected:
    /**
     * \brief Снять с ячейки штатный флажок и текст.
     *
     * Правка живёт именно здесь, а не в paint(): QStyledItemDelegate::paint() заново
     * вызывает initStyleOption() на переданных ему параметрах, поэтому всё, что снято до
     * вызова базового метода, возвращается обратно. Так на экране и оказывался синий
     * системный флажок поверх цветного квадратика.
     */
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;

    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;

private:
    /// \brief Сторона квадратика: ровно как у системного флажка в этом стиле.
    static int swatchSide(const QStyleOptionViewItem &option);
};

} // namespace spotty
