/**
 * \file ShortcutDelegate.h
 * \brief Редактор сочетаний клавиш в ячейке таблицы.
 */
#pragma once

#include <QStyledItemDelegate>

namespace spotty {

/**
 * \class ShortcutDelegate
 * \brief Даёт редактировать сочетание клавиш нажатием самого сочетания.
 *
 * Обычное текстовое поле здесь не годится: пользователь набирал бы «Ctrl+Shift+F5»
 * посимвольно и легко ошибся бы в написании. QKeySequenceEdit ловит настоящее нажатие,
 * поэтому записать несуществующее сочетание невозможно.
 */
class ShortcutDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

    void setEditorData(QWidget *editor, const QModelIndex &index) const override;

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

} // namespace spotty
