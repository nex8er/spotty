/**
 * \file MacroEditDialog.h
 * \brief Диалог правки одного макроса.
 */
#pragma once

#include <MacroStore.h>

#include <QDialog>

class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace spotty {

/**
 * \class MacroEditDialog
 * \brief Правка имени, содержимого, формата, терминации и горячей клавиши макроса.
 *
 * Содержимое вводится многострочным полем: посылки бывают длинными, а шестнадцатеричный
 * дамп удобно вставлять как есть, с переносами.
 */
class MacroEditDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param macro Исходное значение; для нового макроса — пустое.
     */
    explicit MacroEditDialog(const Macro &macro, QWidget *parent = nullptr);

    /// \brief Макрос с внесёнными правками.
    Macro macro() const;

private:
    /// \brief Проверить содержимое и показать ошибку кодирования, если она есть.
    void validate();

    QLineEdit *m_name = nullptr;
    QPlainTextEdit *m_payload = nullptr;
    QComboBox *m_format = nullptr;
    QComboBox *m_termination = nullptr;
    QKeySequenceEdit *m_shortcut = nullptr;
    QLabel *m_error = nullptr;
};

} // namespace spotty
