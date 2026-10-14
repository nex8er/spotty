/**
 * \file ActionPromptDialog.cpp
 * \brief Реализация spotty::ActionPromptDialog.
 */
#include "ActionPromptDialog.h"

#include "../CollapsibleSection.h"
#include "../SchemaForm.h"

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace spotty {

ActionPromptDialog::ActionPromptDialog(const QString &title, const SettingsSchema &schema,
                                       const QVariantMap &initial, QWidget *parent)
    : QDialog(parent)
    , m_schema(schema)
{
    setWindowTitle(title);
    setModal(true);
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    m_form = new SchemaForm(schema, initial, this, /*allowSecrets=*/true);
    layout->addWidget(m_form);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_form, &SchemaForm::valueChanged, this, &ActionPromptDialog::updateOkButton);
    updateOkButton();

    // Раскрытый «Advanced» должен увеличить окно: без прокрутки оно само не растёт, и поля сжимаются
    // ниже минимума. Из очереди — раздел успевает показать содержимое до пересчёта размера. Свёрнутый
    // раздел возвращает окну прежний размер.
    for (CollapsibleSection *section : m_form->findChildren<CollapsibleSection *>()) {
        connect(section, &CollapsibleSection::toggled, this,
                [this] { QTimer::singleShot(0, this, &QWidget::adjustSize); });
    }

    // Курсор — в первое пустое текстовое поле: имя уже стоит от прошлого раза, ждут пароль.
    // Свёрнутые поля пропускаются: фокус на скрытом виджете остался бы незаметным.
    for (QLineEdit *edit : m_form->findChildren<QLineEdit *>()) {
        if (edit->text().isEmpty() && edit->isVisibleTo(this)) {
            edit->setFocus();
            break;
        }
    }
}

QVariantMap ActionPromptDialog::values() const
{
    // Форма хранит и чужие ключи начальных значений, а наружу должно уйти только своё.
    const QVariantMap all = m_form->values();
    QVariantMap result;
    for (const SettingsField &field : m_schema.fields()) {
        if (field.holdsValue() && all.contains(field.key))
            result.insert(field.key, all.value(field.key));
    }
    return result;
}

void ActionPromptDialog::updateOkButton()
{
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_schema.missingRequiredFields(values()).isEmpty());
}

} // namespace spotty
