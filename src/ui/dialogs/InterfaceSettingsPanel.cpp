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
#include <QCompleter>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QEvent>
#include <QStringListModel>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <utility>

namespace spotty {

namespace {

/**
 * \brief Как часто спрашивать плагин о найденном для полей SettingsField::live.
 *
 * Ровно тот же период, с каким ядро перечисляет устройства. Чаще нет смысла: человек не
 * различает появление пункта за 200 мс и за секунду, а каждый вызов ещё и продлевает
 * опрос на стороне плагина.
 */
constexpr int kLiveOptionsIntervalMs = 1000;

} // namespace

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
        // m_populating гасит и обработчик своего же сигнала (см. rebuildList()): без него
        // registry->changed(), поднятый этой самой записью, синхронно перестроил бы список
        // и удалил бы поля прямо из-под ещё выполняющегося обработчика.
        m_populating = true;
        m_registry->setAlias(m_currentId, m_alias->text().trimmed());
        m_populating = false;
    });

    connect(m_hidden, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_populating || m_currentId.isEmpty() || !m_registry)
            return;
        m_populating = true;
        m_registry->setHidden(m_currentId, checked);
        m_populating = false;
    });

    // Список устройств обновляется вместе с реестром: появление, пропажа и правки,
    // сделанные где-то ещё (например, псевдоним из InterfaceBar), не должны требовать
    // переоткрытия панели, чтобы стать видны здесь.
    if (m_registry)
        connect(m_registry, &InterfaceRegistry::changed, this, &InterfaceSettingsPanel::rebuildList);

    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(kLiveOptionsIntervalMs);
    connect(m_liveTimer, &QTimer::timeout, this, &InterfaceSettingsPanel::refreshLiveOptions);

    rebuildList();
}

void InterfaceSettingsPanel::rebuildList()
{
    // Своя же правка (псевдоним, скрытие, поле схемы) поднимает registry->changed()
    // синхронно, прямо из обработчика, который эту правку внёс. Без этой проверки
    // rebuildList() тут же перестраивал бы список и удалял виджет, чей обработчик ещё не
    // вернул управление, — use-after-free в тот же момент, что и падение.
    if (!m_registry || m_populating)
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

void InterfaceSettingsPanel::flagInvalidFields(const QStringList &fieldKeys)
{
    m_invalidFields = fieldKeys;
    m_invalidFieldsForId = m_currentId;
    applyInvalidFieldStyling();
}

void InterfaceSettingsPanel::applyInvalidFieldStyling()
{
    for (auto it = m_editors.constBegin(); it != m_editors.constEnd(); ++it) {
        QWidget *editor = it.value();
        editor->setProperty("fieldInvalid", m_invalidFields.contains(it.key()));
        // setProperty() сам по себе не перекрашивает — стиль читает динамические свойства
        // только при полировке, unpolish()+polish() заставляет её случиться немедленно.
        editor->style()->unpolish(editor);
        editor->style()->polish(editor);
    }
}

void InterfaceSettingsPanel::showEntry(const QString &id)
{
    m_currentId = id;
    m_populating = true;

    // Подсветка привязана к устройству (см. #m_invalidFieldsForId) — переключение на любое
    // другое обнуляет её, иначе чужая ошибка осталась бы гореть на новом наборе полей.
    if (id != m_invalidFieldsForId)
        m_invalidFields.clear();

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

                // Имя редактору дают не ради красоты: у панели несколько списков подряд
                // (переключатель устройств и поля схемы), и отличить их снаружи — из
                // таблицы стилей или из теста — иначе можно только по порядку следования,
                // который меняется от плагина к плагину.
                editor->setObjectName(QStringLiteral("schemaField_") + field.key);
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

    m_liveFields.clear();
    for (const SettingsField &field : m_currentSchema.fields()) {
        if (field.live && m_editors.contains(field.key))
            m_liveFields.append(field.key);
    }

    // Редакторы только что пересозданы — старое свойство fieldInvalid на них не сохранилось
    // (это новые виджеты), даже если m_invalidFields осталась той же (то же устройство).
    applyInvalidFieldStyling();

    m_populating = false;

    // Первый запрос — сразу, не дожидаясь тика таймера: он и показывает уже найденное (при
    // возврате к тому же устройству список обычно непустой), и запускает у плагина опрос,
    // результаты которого появятся через секунду-другую.
    updateLiveTimer();
    if (!m_liveFields.isEmpty())
        refreshLiveOptions();
}

void InterfaceSettingsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateLiveTimer();
    if (!m_liveFields.isEmpty())
        refreshLiveOptions();
}

void InterfaceSettingsPanel::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateLiveTimer();
}

void InterfaceSettingsPanel::updateLiveTimer()
{
    if (!m_liveTimer)
        return;

    // isVisible(), а не isHidden(): панель, встроенная страницей в закрытый диалог общих
    // настроек, не скрыта сама по себе, но и не видна — опрашивать шину ради неё незачем.
    if (m_liveFields.isEmpty() || !isVisible())
        m_liveTimer->stop();
    else if (!m_liveTimer->isActive())
        m_liveTimer->start();
}

void InterfaceSettingsPanel::refreshLiveOptions()
{
    if (m_currentId.isEmpty() || !m_registry || !m_plugins || m_liveFields.isEmpty())
        return;

    const InterfaceEntry *entry = m_registry->entry(m_currentId);
    if (!entry)
        return;

    IInterfacePlugin *plugin = m_plugins->plugin(entry->descriptor.pluginId);
    if (!plugin)
        return;

    const QVariantMap settings = m_registry->settingsFor(m_currentId);
    for (const QString &key : std::as_const(m_liveFields))
        applyLiveOptions(key, plugin->liveOptions(entry->descriptor, key, settings));
}

void InterfaceSettingsPanel::applyLiveOptions(const QString &key,
                                              const QList<SettingsOption> &options)
{
    auto *combo = qobject_cast<QComboBox *>(m_editors.value(key));
    if (!combo)
        return;

    const SettingsField *field = m_currentSchema.field(key);
    if (!field)
        return;

    // Объявленные схемой пункты остаются первыми: у поля может быть и постоянная часть
    // («любой узел»), и найденная опросом.
    QList<SettingsOption> merged = field->options;
    for (const SettingsOption &option : options) {
        const auto known = std::find_if(merged.cbegin(), merged.cend(),
                                        [&option](const SettingsOption &existing) {
                                            return existing.value == option.value;
                                        });
        if (known == merged.cend())
            merged.append(option);
    }

    bool same = merged.size() == combo->count();
    for (int i = 0; same && i < merged.size(); ++i)
        same = combo->itemText(i) == merged.at(i).label && combo->itemData(i) == merged.at(i).value;
    if (same)
        return;

    // Что именно восстанавливать, зависит от того, выбран пункт списка или набран свой
    // номер: у редактируемого списка с набранным текстом currentData() невалиден, и
    // единственное, что несёт выбор пользователя, — сам текст.
    const QVariant selected = combo->currentData();
    const QString typed = combo->currentText();

    // Сохранение прежнего значения флага, а не сброс в false: refreshLiveOptions() зовётся
    // и из showEntry(), где перестроение полей ещё идёт, и обнулить флаг здесь значило бы
    // снять защиту с остатка чужой работы.
    const bool wasPopulating = m_populating;
    m_populating = true;
    {
        const QSignalBlocker blocker(combo);
        combo->clear();
        for (const SettingsOption &option : std::as_const(merged))
            combo->addItem(option.label, option.value);

        const int index = selected.isValid() ? combo->findData(selected) : -1;
        if (index >= 0)
            combo->setCurrentIndex(index);
        else if (combo->isEditable())
            combo->setCurrentText(typed);
        else
            combo->setCurrentIndex(combo->findText(typed));
    }
    m_populating = wasPopulating;
}

void InterfaceSettingsPanel::clearSchemaEditors()
{
    m_editors.clear();

    // deleteLater(), а не delete: m_populating обрывает обычный путь, которым эта правка
    // могла бы дойти до собственного же виджета, но второй слой защиты дешев, а цена ошибки
    // — новый use-after-free, если однажды появится ещё один путь коммита, забывший про
    // m_populating. hide() — отдельно и обязательно: takeAt() убирает виджет из учёта
    // layout'а немедленно, но сам виджет остаётся видимым (просто больше не позиционируется)
    // до отложенного удаления, и старые группы полей на миг накладываются на новые, которые
    // showEntry() тут же добавляет следом, — ровно то же самое use-after-free по духу, только
    // на экране, а не в памяти.
    while (QLayoutItem *item = m_schemaLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

QWidget *InterfaceSettingsPanel::createEditor(const SettingsField &field, const QVariant &value)
{
    switch (field.type) {
    case SettingsField::Choice: {
        // Список масштаба «база устройств J-Link» (тысячи пунктов) не годится ни в
        // addItem() (заметно тормозит открытие), ни целиком в выпадение — обычным полям
        // вроде скорости UART (десяток пунктов) он не грозит, поэтому порог не мешает им.
        constexpr int kLiveSearchThreshold = 20;
        if (field.editable && field.options.size() > kLiveSearchThreshold)
            return createLiveSearchEditor(field, value);

        auto *combo = new QComboBox(this);
        for (const SettingsOption &option : field.options)
            combo->addItem(option.label, option.value);

        if (field.editable) {
            // Редактируемый список нужен там, где перечисление принципиально неполно:
            // нестандартная скорость UART, произвольное имя целевого чипа J-Link. Валидатор
            // годится только числовым полям — строковый (имя устройства) с ним не пропустил
            // бы ни одной буквы.
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            if (field.defaultValue.typeId() == QMetaType::Int)
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

QWidget *InterfaceSettingsPanel::createLiveSearchEditor(const SettingsField &field,
                                                        const QVariant &value)
{
    constexpr int kMaxSuggestions = 10;

    auto *combo = new QComboBox(this);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setEditText(value.toString());

    // Полный список — здесь, а не в самом QComboBox: тысячи QComboBox::addItem() заметно
    // тормозят открытие списка и не нужны — то, что выбрали, и так берётся из текста поля
    // (см. commitSchemaValues(): currentData() у пустой модели невалиден, в ход идёт
    // currentText()), а не из модели комбобокса.
    QStringList allNames;
    allNames.reserve(field.options.size());
    for (const SettingsOption &option : field.options)
        allNames.append(option.label);

    auto *suggestionModel = new QStringListModel(combo);
    auto *completer = new QCompleter(suggestionModel, combo);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    // Подсказки ниже уже отфильтрованы и ограничены сами — просить QCompleter фильтровать
    // их ещё раз его правилом (только по префиксу) незачем и спрятало бы совпадение по
    // подстроке не с самого начала имени.
    completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    combo->setCompleter(completer);

    connect(combo->lineEdit(), &QLineEdit::textEdited, combo,
           [allNames, suggestionModel, completer](const QString &text) {
        QStringList matches;
        if (!text.isEmpty()) {
            for (const QString &name : allNames) {
                if (name.contains(text, Qt::CaseInsensitive)) {
                    matches.append(name);
                    if (matches.size() == kMaxSuggestions)
                        break;
                }
            }
        }
        suggestionModel->setStringList(matches);
        completer->complete();
    });

    connect(combo, &QComboBox::editTextChanged, this, [this] {
        if (!m_populating)
            commitSchemaValues();
    });

    return combo;
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

    m_populating = true;
    m_registry->setSettingsFor(m_currentId, result);
    m_populating = false;

    if (!m_invalidFields.isEmpty()) {
        // Пользователь заполняет поле сам, не через flagInvalidFields() — снимаем подсветку
        // сразу, как только есть что показать, не дожидаясь повторной попытки открыть канал.
        for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
            if (!it.value().toString().trimmed().isEmpty())
                m_invalidFields.removeAll(it.key());
        }
        applyInvalidFieldStyling();
    }

    Q_EMIT settingsApplied(m_currentId);
}

} // namespace spotty
