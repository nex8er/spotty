/**
 * \file InterfaceSettingsPanel.cpp
 * \brief Реализация spotty::InterfaceSettingsPanel.
 */
#include "InterfaceSettingsPanel.h"

#include "../InterfaceLabel.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <spotty/api/IInterfacePlugin.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <climits>

namespace spotty {

InterfaceSettingsPanel::InterfaceSettingsPanel(InterfaceRegistry *registry,
                                               PluginManager *plugins, QWidget *parent)
    : QWidget(parent)
    , m_registry(registry)
    , m_plugins(plugins)
{
    auto *layout = new QVBoxLayout(this);

    auto *switcherForm = new QFormLayout;
    m_deviceCombo = new QComboBox(this);
    switcherForm->addRow(tr("Device"), m_deviceCombo);
    layout->addLayout(switcherForm);

    // Псевдоним, скрытие и служебные сведения принадлежат ядру, а не плагину — есть у
    // любого транспорта, поэтому стоят своей группой над полями схемы.
    auto *infoBox = new QGroupBox(tr("Interface"), this);
    auto *infoForm = new QFormLayout(infoBox);

    m_alias = new QLineEdit(infoBox);
    infoForm->addRow(tr("Alias"), m_alias);

    m_hidden = new QCheckBox(tr("Hide from the interface list"), infoBox);
    infoForm->addRow(QString(), m_hidden);

    m_addressValue = new QLabel(infoBox);
    infoForm->addRow(tr("Address"), m_addressValue);

    m_vidPidValue = new QLabel(infoBox);
    infoForm->addRow(tr("VID:PID"), m_vidPidValue);

    layout->addWidget(infoBox);

    // Поля схемы вставляются сюда — количество и состав групп зависят от плагина
    // выбранного устройства и пересобираются заново при каждом переключении.
    auto *schemaContainer = new QWidget(this);
    m_schemaLayout = new QVBoxLayout(schemaContainer);
    m_schemaLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(schemaContainer);

    layout->addStretch(1);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_populating)
            return;
        showEntry(m_deviceCombo->itemData(index).toString());
    });

    connect(m_alias, &QLineEdit::editingFinished, this, [this] {
        if (m_populating || m_currentId.isEmpty() || !m_registry)
            return;
        m_registry->setAlias(m_currentId, m_alias->text().trimmed());
    });

    connect(m_hidden, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_populating || m_currentId.isEmpty() || !m_registry)
            return;
        m_registry->setHidden(m_currentId, checked);
    });

    // Список устройств обновляется вместе с реестром: появление, пропажа и правки,
    // сделанные где-то ещё (например, псевдоним из InterfaceBar), не должны требовать
    // переоткрытия панели, чтобы стать видны здесь.
    if (m_registry)
        connect(m_registry, &InterfaceRegistry::changed, this, &InterfaceSettingsPanel::rebuildList);

    rebuildList();
}

void InterfaceSettingsPanel::rebuildList()
{
    if (!m_registry)
        return;

    const QString previous =
        m_currentId.isEmpty() ? m_deviceCombo->currentData().toString() : m_currentId;

    m_populating = true;
    const QSignalBlocker blocker(m_deviceCombo);

    m_deviceCombo->clear();
    for (const InterfaceEntry &entry : m_registry->entries())
        m_deviceCombo->addItem(interfacePrimaryLabel(entry, m_plugins), entry.descriptor.id);

    const int restored = m_deviceCombo->findData(previous);
    m_deviceCombo->setCurrentIndex(restored >= 0 ? restored : 0);
    m_populating = false;

    showEntry(m_deviceCombo->currentData().toString());
}

void InterfaceSettingsPanel::selectInterface(const QString &id)
{
    const int index = m_deviceCombo->findData(id);
    if (index >= 0)
        m_deviceCombo->setCurrentIndex(index);
}

void InterfaceSettingsPanel::showEntry(const QString &id)
{
    m_currentId = id;
    m_populating = true;

    const InterfaceEntry *entry = (m_registry && !id.isEmpty()) ? m_registry->entry(id) : nullptr;

    m_alias->setEnabled(entry != nullptr);
    m_hidden->setEnabled(entry != nullptr);

    if (!entry) {
        m_alias->clear();
        m_alias->setPlaceholderText(QString());
        m_hidden->setChecked(false);
        m_addressValue->clear();
        m_vidPidValue->clear();
        clearSchemaEditors();
        m_populating = false;
        return;
    }

    m_alias->setText(entry->alias);
    m_alias->setPlaceholderText(interfaceDefaultName(entry->descriptor));
    m_hidden->setChecked(entry->hidden);
    m_addressValue->setText(entry->descriptor.systemName);

    const QString vendorId =
        entry->descriptor.extra.value(QStringLiteral("vendorId")).toString();
    const QString productId =
        entry->descriptor.extra.value(QStringLiteral("productId")).toString();
    m_vidPidValue->setText(
        (vendorId.isEmpty() && productId.isEmpty())
            ? QStringLiteral("—")
            : QStringLiteral("%1:%2").arg(vendorId.isEmpty() ? QStringLiteral("?") : vendorId,
                                          productId.isEmpty() ? QStringLiteral("?") : productId));

    clearSchemaEditors();

    IInterfacePlugin *plugin = m_plugins ? m_plugins->plugin(entry->descriptor.pluginId) : nullptr;
    if (plugin) {
        m_currentSchema = plugin->settingsSchema();
        const QVariantMap values = m_registry->settingsFor(id);

        // Разделы идут в порядке объявления в схеме: раскладка следует замыслу автора
        // плагина, который знает, какие параметры логично держать рядом.
        for (const QString &group : m_currentSchema.groups()) {
            auto *box = new QGroupBox(group.isEmpty() ? tr("General") : group, this);
            auto *form = new QFormLayout(box);

            for (const SettingsField &field : m_currentSchema.fieldsInGroup(group)) {
                QWidget *editor = createEditor(field, values.value(field.key, field.defaultValue));
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

            m_schemaLayout->addWidget(box);
        }
    } else {
        m_currentSchema = SettingsSchema{};
    }

    m_populating = false;
}

void InterfaceSettingsPanel::clearSchemaEditors()
{
    m_editors.clear();

    while (QLayoutItem *item = m_schemaLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

QWidget *InterfaceSettingsPanel::createEditor(const SettingsField &field, const QVariant &value)
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

        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            if (!m_populating)
                commitSchemaValues();
        });
        if (field.editable) {
            connect(combo, &QComboBox::editTextChanged, this, [this] {
                if (!m_populating)
                    commitSchemaValues();
            });
        }

        return combo;
    }

    case SettingsField::Integer: {
        auto *spin = new QSpinBox(this);
        spin->setRange(field.minimum, field.maximum > field.minimum ? field.maximum : INT_MAX);
        spin->setValue(value.toInt());
        connect(spin, &QSpinBox::valueChanged, this, [this] {
            if (!m_populating)
                commitSchemaValues();
        });
        return spin;
    }

    case SettingsField::Toggle: {
        auto *check = new QCheckBox(this);
        check->setChecked(value.toBool());
        connect(check, &QCheckBox::toggled, this, [this] {
            if (!m_populating)
                commitSchemaValues();
        });
        return check;
    }

    case SettingsField::Text:
        break;
    }

    auto *edit = new QLineEdit(value.toString(), this);
    connect(edit, &QLineEdit::editingFinished, this, [this] {
        if (!m_populating)
            commitSchemaValues();
    });
    return edit;
}

void InterfaceSettingsPanel::commitSchemaValues()
{
    if (m_currentId.isEmpty() || !m_registry)
        return;

    QVariantMap result;
    for (const SettingsField &field : m_currentSchema.fields()) {
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

    m_registry->setSettingsFor(m_currentId, result);
    Q_EMIT settingsApplied(m_currentId);
}

} // namespace spotty
