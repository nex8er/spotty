/**
 * \file InterfaceSettingsDialog.cpp
 * \brief Реализация spotty::InterfaceSettingsDialog.
 */
#include "InterfaceSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <climits>

namespace spotty {

namespace {

/// \brief Имя свойства, в котором редактор запоминает своё поле схемы.
constexpr auto kFieldKeyProperty = "spottyFieldKey";

} // namespace

InterfaceSettingsDialog::InterfaceSettingsDialog(const QString &title,
                                                 const SettingsSchema &schema,
                                                 const QVariantMap &values,
                                                 const QString &alias, QWidget *parent)
    : QDialog(parent)
    , m_schema(schema)
{
    setWindowTitle(tr("Settings - %1").arg(title));
    setModal(true);

    auto *layout = new QVBoxLayout(this);

    // Псевдоним принадлежит ядру, а не плагину: он есть у любого транспорта, поэтому
    // стоит отдельно и всегда сверху.
    auto *aliasBox = new QGroupBox(tr("Interface"), this);
    auto *aliasForm = new QFormLayout(aliasBox);
    m_alias = new QLineEdit(alias, aliasBox);
    m_alias->setPlaceholderText(title);
    aliasForm->addRow(tr("Alias"), m_alias);
    layout->addWidget(aliasBox);

    // Разделы идут в порядке объявления в схеме: раскладка следует замыслу автора
    // плагина, который знает, какие параметры логично держать рядом.
    const QStringList groups = schema.groups();
    for (const QString &group : groups) {
        auto *box = new QGroupBox(group.isEmpty() ? tr("General") : group, this);
        auto *form = new QFormLayout(box);

        const QList<SettingsField> fields = schema.fieldsInGroup(group);
        for (const SettingsField &field : fields) {
            QWidget *editor = createEditor(field, values.value(field.key, field.defaultValue));
            if (!editor)
                continue;

            editor->setProperty(kFieldKeyProperty, field.key);
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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QWidget *InterfaceSettingsDialog::createEditor(const SettingsField &field,
                                               const QVariant &value)
{
    switch (field.type) {
    case SettingsField::Choice: {
        auto *combo = new QComboBox(this);
        for (const SettingsOption &option : field.options)
            combo->addItem(option.label, option.value);

        if (field.editable) {
            // Редактируемый список нужен там, где перечисление принципиально неполно:
            // нестандартная скорость UART, произвольный номер порта.
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            combo->setValidator(new QIntValidator(1, 100'000'000, combo));
        }

        const int index = combo->findData(value);
        if (index >= 0)
            combo->setCurrentIndex(index);
        else if (field.editable)
            combo->setCurrentText(value.toString());

        return combo;
    }

    case SettingsField::Integer: {
        auto *spin = new QSpinBox(this);
        spin->setRange(field.minimum, field.maximum > field.minimum ? field.maximum
                                                                    : INT_MAX);
        spin->setValue(value.toInt());
        return spin;
    }

    case SettingsField::Toggle: {
        auto *check = new QCheckBox(this);
        check->setChecked(value.toBool());
        return check;
    }

    case SettingsField::Text:
        break;
    }

    auto *edit = new QLineEdit(value.toString(), this);
    return edit;
}

QVariantMap InterfaceSettingsDialog::values() const
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
                // Значение, введённое руками в редактируемый список. Приводим к типу
                // значения по умолчанию: иначе «115200» осталось бы строкой и канал
                // получил бы не то, чего ждёт.
                const QString text = combo->currentText();
                result.insert(field.key,
                              field.defaultValue.typeId() == QMetaType::Int
                                  ? QVariant(text.toInt())
                                  : QVariant(text));
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

QString InterfaceSettingsDialog::alias() const
{
    return m_alias->text().trimmed();
}

} // namespace spotty
