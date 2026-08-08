/**
 * \file SchemaForm.cpp
 * \brief Реализация spotty::SchemaForm.
 */
#include "SchemaForm.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <climits>

namespace spotty {

SchemaForm::SchemaForm(const SettingsSchema &schema, const QVariantMap &values, QWidget *parent)
    : QWidget(parent)
    , m_schema(schema)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    const QVariantMap effective = schema.normalized(values);

    // Разделы идут в порядке объявления в схеме: раскладка следует замыслу автора
    // плагина, который знает, какие параметры логично держать рядом.
    for (const QString &group : schema.groups()) {
        auto *box = new QGroupBox(group.isEmpty() ? tr("General") : group, this);
        auto *form = new QFormLayout(box);

        for (const SettingsField &field : schema.fieldsInGroup(group)) {
            QWidget *editor = createEditor(field, effective.value(field.key, field.defaultValue));
            if (!editor)
                continue;

            m_editors.insert(field.key, editor);

            if (field.suffix.isEmpty()) {
                form->addRow(field.label, editor);
            } else {
                auto *row = new QWidget(box);
                auto *rowLayout = new QHBoxLayout(row);
                rowLayout->setContentsMargins(0, 0, 0, 0);
                rowLayout->addWidget(editor, 1);
                rowLayout->addWidget(new QLabel(field.suffix, row));
                form->addRow(field.label, row);
            }

            if (!field.hint.isEmpty()) {
                auto *hint = new QLabel(field.hint, box);
                hint->setObjectName(QStringLiteral("hintLabel"));
                hint->setWordWrap(true);
                form->addRow(QString(), hint);
            }
        }

        layout->addWidget(box);
    }

    layout->addStretch(1);
}

QWidget *SchemaForm::createEditor(const SettingsField &field, const QVariant &value)
{
    switch (field.type) {
    case SettingsField::Choice: {
        auto *combo = new QComboBox(this);
        for (const SettingsOption &option : field.options)
            combo->addItem(option.label, option.value);

        if (field.editable) {
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
        }

        const int index = combo->findData(value);
        if (index >= 0)
            combo->setCurrentIndex(index);
        else if (field.editable)
            combo->setCurrentText(value.toString());

        connect(combo, &QComboBox::currentIndexChanged, this, &SchemaForm::valueChanged);
        return combo;
    }

    case SettingsField::Integer: {
        auto *spin = new QSpinBox(this);
        spin->setRange(field.minimum, field.maximum > field.minimum ? field.maximum : INT_MAX);
        spin->setValue(value.toInt());
        connect(spin, &QSpinBox::valueChanged, this, &SchemaForm::valueChanged);
        return spin;
    }

    case SettingsField::Toggle: {
        auto *check = new QCheckBox(this);
        check->setChecked(value.toBool());
        connect(check, &QCheckBox::toggled, this, &SchemaForm::valueChanged);
        return check;
    }

    case SettingsField::Text:
        break;
    }

    auto *edit = new QLineEdit(value.toString(), this);
    connect(edit, &QLineEdit::textChanged, this, &SchemaForm::valueChanged);
    return edit;
}

QVariantMap SchemaForm::values() const
{
    QVariantMap result;

    for (const SettingsField &field : m_schema.fields()) {
        QWidget *editor = m_editors.value(field.key);
        if (!editor)
            continue;

        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            const QVariant data = combo->currentData();
            if (data.isValid()) {
                result.insert(field.key, data);
            } else {
                // Значение, набранное руками в редактируемом списке. Приводим к типу
                // умолчания: иначе число уехало бы в настройки строкой и при следующем
                // чтении не совпало бы ни с одним пунктом.
                QVariant typed = combo->currentText();
                if (field.defaultValue.isValid())
                    typed.convert(field.defaultValue.metaType());
                result.insert(field.key, typed);
            }
        } else if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
            result.insert(field.key, spin->value());
        } else if (auto *check = qobject_cast<QCheckBox *>(editor)) {
            result.insert(field.key, check->isChecked());
        } else if (auto *edit = qobject_cast<QLineEdit *>(editor)) {
            result.insert(field.key, edit->text());
        }
    }

    return result;
}

} // namespace spotty
