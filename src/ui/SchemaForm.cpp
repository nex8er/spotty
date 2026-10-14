/**
 * \file SchemaForm.cpp
 * \brief Реализация spotty::SchemaForm.
 */
#include "SchemaForm.h"

#include "CollapsibleSection.h"

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

SchemaForm::SchemaForm(const SettingsSchema &schema, const QVariantMap &values, QWidget *parent,
                       bool allowSecrets)
    : QWidget(parent)
    , m_schema(schema)
    , m_allowSecrets(allowSecrets)
    , m_initialValues(values)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    const QVariantMap effective = schema.normalized(values);

    // Строка формы под одно поле: подпись, редактор, единица измерения и пояснение.
    auto addRow = [](QFormLayout *form, QWidget *owner, const SettingsField &field, QWidget *editor) {
        if (field.label.isEmpty() && field.type == SettingsField::Note) {
            form->addRow(editor); // пояснение на всю ширину
        } else if (field.type == SettingsField::Toggle) {
            form->addRow(QString(), editor); // подпись на самом флажке, как в панели настроек интерфейса
        } else if (field.suffix.isEmpty()) {
            form->addRow(field.label, editor);
        } else {
            auto *row = new QWidget(owner);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->addWidget(editor, 1);
            rowLayout->addWidget(new QLabel(field.suffix, row));
            form->addRow(field.label, row);
        }

        if (!field.hint.isEmpty()) {
            auto *hint = new QLabel(field.hint, owner);
            hint->setObjectName(QStringLiteral("hintLabel"));
            hint->setWordWrap(true);
            form->addRow(QString(), hint);
        }
    };

    // Разделы идут в порядке объявления в схеме: раскладка следует замыслу автора
    // плагина, который знает, какие параметры логично держать рядом.
    for (const QString &group : schema.groups()) {
        auto *box = new QGroupBox(group.isEmpty() ? tr("General") : group, this);
        auto *form = new QFormLayout(box);
        CollapsibleSection *advanced = nullptr;

        for (const SettingsField &field : schema.fieldsInGroup(group)) {
            QWidget *editor = createEditor(field, effective.value(field.key, field.defaultValue));
            if (!editor)
                continue;

            m_editors.insert(field.key, editor);

            if (field.advanced) {
                // Редкие поля группы — под общим заголовком «Advanced», свёрнутым.
                if (!advanced) {
                    advanced = new CollapsibleSection(tr("Advanced"), box);
                    form->addRow(advanced);
                }
                addRow(advanced->form(), advanced, field, editor);
            } else {
                addRow(form, box, field, editor);
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
        // Ширина поля — от формы, а не от самого длинного пункта: названия портов вроде
        // «cu.usbmodem0000593034541 — J-Link» длинные, а пункты к тому же добавляются уже
        // при открытом окне, и форма не должна расти или дёргаться вслед за ними.
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(12);
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
        auto *check = new QCheckBox(field.label, this);
        check->setChecked(value.toBool());
        connect(check, &QCheckBox::toggled, this, &SchemaForm::valueChanged);
        return check;
    }

    case SettingsField::Note: {
        auto *note = new QLabel(field.defaultValue.toString(), this);
        note->setWordWrap(true);
        note->setOpenExternalLinks(true);
        return note;
    }

    case SettingsField::Action:
        // Нажатие передать некому: у панельного плагина нет аналога
        // IInterfacePlugin::triggerAction(), а мёртвая кнопка хуже отсутствующей.
        return nullptr;

    case SettingsField::Password: {
        // Настройки панели пишутся на диск как есть, а Password обещает обратное. Честнее не
        // показать поле вовсе, чем собрать пароль и положить его в читаемый файл. Форме вопроса
        // перед действием поля разрешены: оттуда ничего не сохраняется.
        if (!m_allowSecrets)
            return nullptr;
        auto *edit = new QLineEdit(value.toString(), this);
        edit->setEchoMode(QLineEdit::Password);
        connect(edit, &QLineEdit::textChanged, this, &SchemaForm::valueChanged);
        return edit;
    }

    case SettingsField::Text:
        break;
    }

    auto *edit = new QLineEdit(value.toString(), this);
    connect(edit, &QLineEdit::textChanged, this, &SchemaForm::valueChanged);
    return edit;
}

QVariant schemaEditorValue(const QWidget *editor, const SettingsField &field)
{
    if (const auto *combo = qobject_cast<const QComboBox *>(editor)) {
        // Набранный текст сильнее пункта, выбранного раньше: при вводе currentIndex() не меняется
        // (вставки нет, InsertPolicy — NoInsert), и без этой проверки своя скорость или свой адрес,
        // набранные после выбора из списка, молча терялись.
        const bool typedOver = combo->isEditable()
                               && combo->currentText() != combo->itemText(combo->currentIndex());
        const QVariant data = combo->currentData();
        if (data.isValid() && !typedOver)
            return data;
        // Набранное руками приводим к типу умолчания: иначе число уехало бы в настройки строкой и
        // при следующем чтении не совпало бы ни с одним пунктом.
        QVariant typed = combo->currentText();
        if (field.defaultValue.isValid())
            typed.convert(field.defaultValue.metaType());
        return typed;
    }
    if (const auto *spin = qobject_cast<const QSpinBox *>(editor))
        return spin->value();
    if (const auto *check = qobject_cast<const QCheckBox *>(editor))
        return check->isChecked();
    if (const auto *edit = qobject_cast<const QLineEdit *>(editor))
        return edit->text();
    return {};
}

QVariantMap SchemaForm::values() const
{
    // Схема описывает только редкие общие параметры, а панель может хранить рядом своё
    // рабочее состояние. Сохраняем незнакомые форме ключи, иначе нажатие OK стирало бы
    // профиль, положение разделителя и другие настройки самого плагина.
    QVariantMap result = m_initialValues;
    for (const SettingsField &field : m_schema.fields()) {
        const QVariant value = schemaEditorValue(m_editors.value(field.key), field);
        if (value.isValid())
            result.insert(field.key, value);
    }
    return result;
}

} // namespace spotty
