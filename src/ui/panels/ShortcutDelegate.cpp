/**
 * \file ShortcutDelegate.cpp
 * \brief Реализация spotty::ShortcutDelegate.
 */
#include "ShortcutDelegate.h"

#include <QKeySequenceEdit>

namespace spotty {

QWidget *ShortcutDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                        const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);

    auto *editor = new QKeySequenceEdit(parent);
    // Одно сочетание, а не последовательность из нескольких: макрос вызывается одним
    // нажатием, и «F1, потом F2» здесь лишено смысла.
    editor->setMaximumSequenceLength(1);
    return editor;
}

void ShortcutDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if (auto *keyEdit = qobject_cast<QKeySequenceEdit *>(editor))
        keyEdit->setKeySequence(QKeySequence(index.data(Qt::EditRole).toString()));
}

void ShortcutDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                    const QModelIndex &index) const
{
    auto *keyEdit = qobject_cast<QKeySequenceEdit *>(editor);
    if (!keyEdit)
        return;

    // Переносимая запись, а не платформенная: на macOS QKeySequence по умолчанию
    // отдаёт «⌘F1», и такая строка не прочиталась бы обратно на других системах.
    model->setData(index, keyEdit->keySequence().toString(QKeySequence::PortableText),
                   Qt::EditRole);
}

} // namespace spotty
