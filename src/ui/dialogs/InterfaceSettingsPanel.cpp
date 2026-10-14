/**
 * \file InterfaceSettingsPanel.cpp
 * \brief Реализация spotty::InterfaceSettingsPanel.
 */
#include "InterfaceSettingsPanel.h"

#include "../CollapsibleSection.h"
#include "../InterfaceLabel.h"
#include "../SchemaForm.h"
#include "ActionPromptDialog.h"

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
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
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

/// \brief Предельная ширина короткого поля (SettingsField::compact), px: порт, а не адрес.
constexpr int kCompactWidth = 110;

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

    // Список устройств и кнопки заведения и удаления подключений — одной строкой: это один
    // жест «выбрать или создать устройство», а не два отдельных блока.
    auto *switcherRow = new QWidget(this);
    auto *switcherRowLayout = new QHBoxLayout(switcherRow);
    switcherRowLayout->setContentsMargins(0, 0, 0, 0);
    switcherRowLayout->addWidget(m_deviceCombo, 1);

    m_addButton = new QPushButton(tr("New connection…"), switcherRow);
    m_addButton->setObjectName(QStringLiteral("addConnectionButton"));
    m_addButton->setAutoDefault(false);
    m_addButton->setVisible(!userDevicePlugins().isEmpty());
    switcherRowLayout->addWidget(m_addButton);

    m_removeButton = new QPushButton(tr("Remove"), switcherRow);
    m_removeButton->setObjectName(QStringLiteral("removeConnectionButton"));
    m_removeButton->setAutoDefault(false);
    m_removeButton->setToolTip(tr("Remove this saved connection. Nothing is changed on the remote side."));
    m_removeButton->setVisible(!userDevicePlugins().isEmpty());
    m_removeButton->setEnabled(false);
    switcherRowLayout->addWidget(m_removeButton);

    switcherForm->addRow(tr("Device"), switcherRow);
    layout->addLayout(switcherForm);

    // Всё, что ниже переключателя устройств, лежит в области прокрутки. Число полей схемы
    // задаёт плагин, а не мы: у UART их хватало с запасом, у плагина с двумя портами и
    // подсказками под каждым — уже нет. Без прокрутки Qt не выходит за размеры окна, а
    // ужимает виджеты ниже их минимума, и поля налезают друг на друга, а подсказки
    // обрезаются до одной строки.
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("schemaScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // Горизонтальная полоса — по необходимости. Наименьшая ширина формы зависит от шрифта и
    // масштаба системы (на Windows она заметно больше, чем на macOS), и узкое окно без
    // полосы обрезало бы правый край полей молча. Содержимое при этом не сжимается ниже
    // своего минимума, а недостающее доступно прокруткой.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(content);

    // setWidget() включает заливку фона у содержимого, и оно становится цветом палитры, а не
    // диалога: на тёмной теме это два чуть разных серых. Прозрачными оба слоя показывают тот
    // фон, который задан диалогу таблицей стилей.
    scroll->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);

    layout->addWidget(scroll, 1);

    // Псевдоним, скрытие и служебные сведения принадлежат ядру, а не плагину — есть у
    // любого транспорта, поэтому стоят своей группой над полями схемы.
    auto *infoBox = new QGroupBox(tr("Interface"), content);
    auto *infoForm = new QFormLayout(infoBox);
    m_infoForm = infoForm;

    m_alias = new QLineEdit(infoBox);
    infoForm->addRow(tr("Alias"), m_alias);

    m_hidden = new QCheckBox(tr("Hide from the interface list"), infoBox);
    infoForm->addRow(QString(), m_hidden);

    m_addressValue = new QLabel(infoBox);
    infoForm->addRow(tr("Address"), m_addressValue);

    m_vidPidValue = new QLabel(infoBox);
    infoForm->addRow(tr("VID:PID"), m_vidPidValue);

    contentLayout->addWidget(infoBox);

    // Поля схемы вставляются сюда — количество и состав групп зависят от плагина
    // выбранного устройства и пересобираются заново при каждом переключении.
    auto *schemaContainer = new QWidget(content);
    m_schemaLayout = new QVBoxLayout(schemaContainer);
    m_schemaLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(schemaContainer);

    contentLayout->addStretch(1);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_populating)
            return;
        showEntry(m_deviceCombo->itemData(index).toString());
    });

    connect(m_addButton, &QPushButton::clicked, this, [this] {
        const auto plugins = userDevicePlugins();
        if (plugins.size() == 1) {
            addUserDevice(plugins.first().first);
            return;
        }
        // Несколько транспортов, где подключения заводит пользователь: спрашиваем, какого.
        QMenu menu(this);
        for (const auto &plugin : plugins) {
            const QString pluginId = plugin.first;
            menu.addAction(plugin.second, this, [this, pluginId] { addUserDevice(pluginId); });
        }
        menu.exec(m_addButton->mapToGlobal(QPoint(0, m_addButton->height())));
    });
    connect(m_removeButton, &QPushButton::clicked, this,
            &InterfaceSettingsPanel::removeCurrentUserDevice);

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

QList<QPair<QString, QString>> InterfaceSettingsPanel::userDevicePlugins() const
{
    QList<QPair<QString, QString>> result;
    if (!m_plugins)
        return result;
    for (IInterfacePlugin *plugin : m_plugins->plugins()) {
        if (plugin->supportsUserDevices())
            result.append({plugin->pluginId(), plugin->displayName()});
    }
    return result;
}

void InterfaceSettingsPanel::addUserDevice(const QString &pluginId)
{
    if (!m_registry)
        return;
    const QString id = m_registry->addUserDevice(pluginId);
    if (id.isEmpty())
        return;
    m_createdIds.append(id);
    selectInterface(id);
}

void InterfaceSettingsPanel::removeCurrentUserDevice()
{
    if (m_currentId.isEmpty() || !m_registry)
        return;
    const InterfaceEntry *entry = m_registry->entry(m_currentId);
    if (!entry || !entry->userDefined)
        return;

    // Спрашиваем всегда: удаление необратимо, а настройки подключения — это и адрес, и
    // параметры, которые придётся вводить заново. Текст общий для любого транспорта; что
    // именно остаётся на самом устройстве, ядро знать не может.
    const QString name = interfacePrimaryLabel(*entry, m_plugins);
    QMessageBox box(QMessageBox::Question, tr("Remove connection"),
                    tr("Remove \"%1\" from Spotty?").arg(name),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText(tr("Only the saved connection is removed. Nothing is changed on the "
                              "remote side."));
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes)
        return;

    m_createdIds.removeAll(m_currentId);
    const QString id = m_currentId;
    m_currentId.clear();
    m_registry->removeUserDevice(id);
}

void InterfaceSettingsPanel::markCreated(const QString &id)
{
    if (!m_createdIds.contains(id))
        m_createdIds.append(id);
}

void InterfaceSettingsPanel::discardUnconfigured()
{
    if (!m_registry || !m_plugins)
        return;

    const QStringList created = m_createdIds;
    m_createdIds.clear();
    for (const QString &id : created) {
        const InterfaceEntry *entry = m_registry->entry(id);
        if (!entry || !entry->userDefined)
            continue;
        IInterfacePlugin *plugin = m_plugins->plugin(entry->descriptor.pluginId);
        if (!plugin)
            continue;
        if (!plugin->settingsSchema().missingRequiredFields(m_registry->settingsFor(id)).isEmpty())
            m_registry->removeUserDevice(id);
    }
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
    m_removeButton->setEnabled(entry && entry->userDefined);
    // У подключения по сети нет ни производителя, ни модели: строка из одних прочерков
    // только занимала бы место.
    m_infoForm->setRowVisible(m_vidPidValue, !(entry && entry->userDefined));
    // Адрес подключения, заведённого пользователем, — та же подпись, что стоит в списке и в
    // псевдониме: второй раз тот же текст ничего не сообщает.
    m_infoForm->setRowVisible(m_addressValue, !(entry && entry->userDefined));

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

        // Формы всех групп и их «Advanced»: подписи в них выравниваются по одной колонке (см. alignFormLabels()).
        QList<QFormLayout *> forms{m_infoForm};

        // Разделы идут в порядке объявления в схеме: раскладка следует замыслу автора плагина,
        // который знает, какие параметры логично держать рядом.
        for (const QString &group : m_currentSchema.groups()) {
            auto *box = new QGroupBox(group.isEmpty() ? tr("General") : group, this);
            auto *form = new QFormLayout(box);
            forms.append(form);
            CollapsibleSection *advanced = nullptr;

            // Строка формы под одно поле или под поле с короткими соседями справа.
            struct Row
            {
                const SettingsField *main;
                QList<const SettingsField *> beside;
            };
            const QList<SettingsField> fields = m_currentSchema.fieldsInGroup(group);
            QList<Row> rows;
            for (const SettingsField &field : fields) {
                // Компактное поле встаёт справа от предыдущего той же области (обычной или «Advanced»).
                // Строке сведений соседи ни к чему, а рядом с кнопкой встаёт только кнопка: поле справа
                // от неё выглядело бы чужим.
                bool canJoin = field.compact && !rows.isEmpty();
                if (canJoin) {
                    const SettingsField *last = rows.last().main;
                    canJoin = last->advanced == field.advanced && last->type != SettingsField::Note
                              && (last->type != SettingsField::Action || field.type == SettingsField::Action);
                }
                if (canJoin)
                    rows.last().beside.append(&field);
                else
                    rows.append(Row{&field, {}});
            }

            for (const Row &row : std::as_const(rows)) {
                QFormLayout *target = form;
                QWidget *owner = box;
                if (row.main->advanced) {
                    // Редкие поля группы — под общим заголовком «Advanced», свёрнутым.
                    if (!advanced) {
                        advanced = new CollapsibleSection(tr("Advanced"), box);
                        advanced->setObjectName(QStringLiteral("advancedSection"));
                        form->addRow(advanced);
                        forms.append(advanced->form());
                    }
                    target = advanced->form();
                    owner = advanced;
                }

                QWidget *editor = createEditor(*row.main, values.value(row.main->key, row.main->defaultValue));
                if (!editor)
                    continue;

                // Имя редактору дают не ради красоты: у панели несколько списков подряд
                // (переключатель устройств и поля схемы), и отличить их снаружи — из таблицы
                // стилей или из теста — иначе можно только по порядку следования, который
                // меняется от плагина к плагину.
                editor->setObjectName(QStringLiteral("schemaField_") + row.main->key);
                m_editors.insert(row.main->key, editor);

                // Соседи справа: «подпись + узкий редактор» или кнопка в той же строке. Ряд из одних
                // кнопок тоже строка-контейнер: кнопке нужна её природная ширина, а не вся строка формы.
                const bool mainIsAction = row.main->type == SettingsField::Action;
                QWidget *rowWidget = editor;
                if (!row.beside.isEmpty() || mainIsAction) {
                    rowWidget = new QWidget(owner);
                    auto *rowLayout = new QHBoxLayout(rowWidget);
                    rowLayout->setContentsMargins(0, 0, 0, 0);
                    rowLayout->addWidget(editor, mainIsAction ? 0 : 1);
                    for (const SettingsField *extra : std::as_const(row.beside)) {
                        QWidget *extraEditor = createEditor(*extra, values.value(extra->key, extra->defaultValue));
                        if (!extraEditor)
                            continue;
                        extraEditor->setObjectName(QStringLiteral("schemaField_") + extra->key);
                        m_editors.insert(extra->key, extraEditor);
                        // Пояснение соседа — подсказкой: строка под рядом встала бы под основным полем
                        // и читалась бы как пояснение к нему.
                        extraEditor->setToolTip(extra->hint);
                        if (extra->type == SettingsField::Action) {
                            rowLayout->addWidget(extraEditor);
                            continue;
                        }
                        extraEditor->setMaximumWidth(kCompactWidth);
                        if (auto *combo = qobject_cast<QComboBox *>(extraEditor)) {
                            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                            combo->setMinimumContentsLength(5);
                        }
                        auto *extraLabel = new QLabel(extra->label, rowWidget);
                        extraLabel->setBuddy(extraEditor);
                        rowLayout->addWidget(extraLabel);
                        rowLayout->addWidget(extraEditor);
                        if (!extra->suffix.isEmpty())
                            rowLayout->addWidget(new QLabel(extra->suffix, rowWidget));
                    }
                    if (mainIsAction)
                        rowLayout->addStretch(1);
                }

                if (mainIsAction || row.main->type == SettingsField::Toggle) {
                    // Подпись уже на самой кнопке или флажке; повторять её слева незачем. Флажок с
                    // собственной подписью нажимается и по тексту, и длинное название не растягивает
                    // колонку подписей всей формы.
                    target->addRow(QString(), rowWidget);
                } else if (row.main->suffix.isEmpty() || !row.beside.isEmpty()) {
                    target->addRow(row.main->label, rowWidget);
                } else {
                    auto *suffixRow = new QWidget(owner);
                    auto *suffixLayout = new QHBoxLayout(suffixRow);
                    suffixLayout->setContentsMargins(0, 0, 0, 0);
                    suffixLayout->addWidget(rowWidget, 1);
                    suffixLayout->addWidget(new QLabel(row.main->suffix, suffixRow));
                    target->addRow(row.main->label, suffixRow);
                }

                if (!row.main->hint.isEmpty()) {
                    auto *hint = new QLabel(row.main->hint, owner);
                    hint->setObjectName(QStringLiteral("hintLabel"));
                    hint->setWordWrap(true);
                    target->addRow(QString(), hint);
                }
            }

            m_schemaLayout->addWidget(box);
        }
        alignFormLabels(forms);
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
    // Строка сведений: опрос приносит её текст, а не пункты. Пустой ответ — плагину пока
    // нечего сказать, и прежний текст остаётся.
    if (auto *note = qobject_cast<QLabel *>(m_editors.value(key))) {
        if (!options.isEmpty())
            applyNote(note, options.first());
        return;
    }

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

void InterfaceSettingsPanel::applyNote(QLabel *note, const SettingsOption &option)
{
    // Невалидное или неизвестное значение — нейтрально: так отвечают плагины, которые о подаче
    // ничего не знают.
    int severity = option.value.isValid() ? option.value.toInt() : 0;
    if (severity < int(NoteSeverity::Neutral) || severity > int(NoteSeverity::Error))
        severity = int(NoteSeverity::Neutral);

    // Значок идёт перед текстом, а цвет — свойством для таблицы стилей: состояние не должно
    // держаться на одном цвете.
    static const char *const kNames[] = {"", "ok", "warning", "error"};
    const QString glyphs[] = {QString(), QStringLiteral("\u2713"), QStringLiteral("\u26A0"),
                              QStringLiteral("\u2715")};
    const QString text = severity == int(NoteSeverity::Neutral)
                             ? option.label
                             : glyphs[severity] + QLatin1Char(' ') + option.label;

    const QByteArray name = kNames[severity];
    const bool sameSeverity = note->property("severity").toByteArray() == name;
    if (sameSeverity && note->text() == text)
        return;

    note->setText(text);
    if (!sameSeverity) {
        note->setProperty("severity", name);
        // Свойство само не перекрашивает: стиль читает его только при полировке.
        note->style()->unpolish(note);
        note->style()->polish(note);
    }
}

void InterfaceSettingsPanel::alignFormLabels(const QList<QFormLayout *> &forms)
{
    // У каждой группы своя форма, а значит и своя колонка подписей: поля «Host» и «Network»
    // начинались бы с разных мест, хотя стоят друг под другом. Одна ширина на все формы
    // возвращает им общий левый край.
    const auto labelAt = [](QFormLayout *form, int row) -> QLabel * {
        QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
        return item ? qobject_cast<QLabel *>(item->widget()) : nullptr;
    };

    int widest = 0;
    for (QFormLayout *form : forms) {
        for (int row = 0; row < form->rowCount(); ++row) {
            if (QLabel *label = labelAt(form, row))
                widest = std::max(widest, label->sizeHint().width());
        }
    }
    for (QFormLayout *form : forms) {
        for (int row = 0; row < form->rowCount(); ++row) {
            if (QLabel *label = labelAt(form, row))
                label->setMinimumWidth(widest);
        }
    }
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
    // Правка редактора пишется в реестр сразу, кроме перестроения полей самой панелью.
    const auto commit = [this] {
        if (!m_populating)
            commitSchemaValues();
    };

    switch (field.type) {
    case SettingsField::Choice: {
        // Список масштаба «база устройств J-Link» (тысячи пунктов) не годится ни в
        // addItem() (заметно тормозит открытие), ни целиком в выпадение — обычным полям
        // вроде скорости UART (десяток пунктов) он не грозит, поэтому порог не мешает им.
        constexpr int kLiveSearchThreshold = 20;
        if (field.editable && field.options.size() > kLiveSearchThreshold)
            return createLiveSearchEditor(field, value);

        auto *combo = new QComboBox(this);
        // Ширина поля — от формы, а не от самого длинного пункта: названия портов вроде
        // «cu.usbmodem0000593034541 — J-Link» длинные, а пункты к тому же добавляются уже
        // при открытом окне, и форма не должна расти или дёргаться вслед за ними.
        
        combo->setMinimumContentsLength(12);
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

        connect(combo, &QComboBox::currentIndexChanged, this, commit);
        if (field.editable)
            connect(combo, &QComboBox::editTextChanged, this, commit);

        return combo;
    }

    case SettingsField::Integer: {
        auto *spin = new QSpinBox(this);
        spin->setRange(field.minimum, field.maximum > field.minimum ? field.maximum : INT_MAX);
        spin->setValue(value.toInt());
        connect(spin, &QSpinBox::valueChanged, this, commit);
        return spin;
    }

    case SettingsField::Toggle: {
        auto *check = new QCheckBox(field.label, this);
        check->setChecked(value.toBool());
        connect(check, &QCheckBox::toggled, this, commit);
        return check;
    }

    case SettingsField::Note: {
        auto *note = new QLabel(field.defaultValue.toString(), this);
        note->setWordWrap(true);
        // Ссылка в сведениях — обычно адрес, откуда взять недостающее (драйвер), и
        // показывать её некликабельной значило бы заставлять перепечатывать.
        note->setTextFormat(Qt::AutoText);
        note->setOpenExternalLinks(true);
        note->setTextInteractionFlags(Qt::TextBrowserInteraction);
        return note;
    }

    case SettingsField::Action: {
        auto *button = new QPushButton(field.label, this);
        // Первая кнопка диалога становится «по умолчанию» сама, а тема заливает такую
        // акцентом — единственное отступление от монохромности. Действие «удалить порт» —
        // последнее, что стоит выделять как главное, и Enter не должен его нажимать.
        button->setAutoDefault(false);
        // Главное действие группы — контур акцента из таблицы стилей, не заливка: заливка у кнопки
        // по умолчанию самого окна.
        if (field.primary)
            button->setProperty("primary", true);
        const QString key = field.key;
        connect(button, &QPushButton::clicked, this, [this, key] { triggerAction(key); });
        return button;
    }

    case SettingsField::Password: {
        auto *edit = new QLineEdit(value.toString(), this);
        edit->setEchoMode(QLineEdit::Password);
        connect(edit, &QLineEdit::editingFinished, this, commit);
        return edit;
    }

    case SettingsField::Text:
        break;
    }

    auto *edit = new QLineEdit(value.toString(), this);
    connect(edit, &QLineEdit::editingFinished, this, commit);
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

void InterfaceSettingsPanel::triggerAction(const QString &key)
{
    if (m_currentId.isEmpty() || !m_registry || !m_plugins)
        return;

    const InterfaceEntry *entry = m_registry->entry(m_currentId);
    if (!entry)
        return;

    IInterfacePlugin *plugin = m_plugins->plugin(entry->descriptor.pluginId);
    if (!plugin)
        return;

    const SettingsField *field = m_currentSchema.field(key);
    QVariantMap settings = m_registry->settingsFor(m_currentId);

    // Сведения, которые нужны одному нажатию и не должны оседать в настройках (имя и пароль ssh),
    // спрашиваются отдельным окном. Прежний ввод держится в памяти этого окна настроек и
    // подставляется снова: три действия подряд не должны требовать трёх паролей. С закрытием
    // окна он исчезает вместе с панелью, ни на диск, ни в реестр не попадая.
    const SettingsSchema prompt = plugin->promptSchema(key, settings);
    if (!prompt.isEmpty()) {
        ActionPromptDialog dialog(field ? field->label : QString(), prompt, m_promptMemory.value(m_currentId), this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        m_promptMemory.insert(m_currentId, dialog.values());
        settings.insert(dialog.values());
    }

    const QString problem = plugin->triggerAction(entry->descriptor, key, settings);

    // Действие обычно меняет то, о чём рассказывают живые поля (поставили драйвер, создали
    // пару портов): спросить сразу, а не через секунду, когда пользователь уже решит, что
    // ничего не произошло.
    if (!m_liveFields.isEmpty())
        refreshLiveOptions();

    if (!problem.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, field ? field->label : QString(), problem,
                        QMessageBox::Ok, this);
        box.setTextFormat(Qt::AutoText);
        box.exec();
    }
}

void InterfaceSettingsPanel::commitSchemaValues()
{
    if (m_currentId.isEmpty() || !m_registry)
        return;

    QVariantMap result;
    for (const SettingsField &field : m_currentSchema.fields()) {
        const QVariant value = schemaEditorValue(m_editors.value(field.key), field);
        if (value.isValid())
            result.insert(field.key, value);
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
