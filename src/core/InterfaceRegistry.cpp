/**
 * \file InterfaceRegistry.cpp
 * \brief Реализация spotty::InterfaceRegistry.
 */
#include "InterfaceRegistry.h"

#include "PluginManager.h"
#include "settings/SettingsStore.h"

#include <spotty/api/IInterfacePlugin.h>

#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>

namespace spotty {

/// \brief Категория журналирования: `spotty.registry`.
Q_LOGGING_CATEGORY(lcRegistry, "spotty.registry")

namespace {

/**
 * \brief Период опроса для плагинов без уведомителя устройств, мс.
 *
 * Достаточно часто, чтобы подключение кабеля ощущалось мгновенным, и достаточно редко,
 * чтобы enumerate() не был заметен в профиле.
 */
constexpr int kPollIntervalMs = 1000;

} // namespace

InterfaceRegistry::InterfaceRegistry(PluginManager *plugins, SettingsStore *store, QObject *parent)
    : QObject(parent)
    , m_plugins(plugins)
    , m_store(store)
{
    Q_ASSERT(m_plugins);
    Q_ASSERT(m_store);
}

InterfaceRegistry::~InterfaceRegistry() = default;

void InterfaceRegistry::start()
{
    bool needsPolling = false;
    for (IInterfacePlugin *plugin : m_plugins->plugins()) {
        QObject *notifier = plugin->hotplugNotifier();
        if (!notifier) {
            needsPolling = true;
            continue;
        }
        // Подключение по имени сигнала: SDK намеренно не вводит базовый класс уведомителя,
        // а лишь обещает, что объект испускает devicesChanged().
        if (!connect(notifier, SIGNAL(devicesChanged()), this, SLOT(refresh()))) {
            // Молча вернуться к опросу нельзя: автор плагина должен узнать, что его
            // уведомитель не работает, иначе он будет считать, что hotplug у него есть.
            qCWarning(lcRegistry) << plugin->pluginId()
                                  << "returned a hotplug notifier without a devicesChanged()"
                                     " signal - falling back to polling";
            needsPolling = true;
        }
    }

    if (needsPolling) {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setInterval(kPollIntervalMs);
        connect(m_pollTimer, &QTimer::timeout, this, &InterfaceRegistry::refresh);
        m_pollTimer->start();
    }

    refresh();
}

void InterfaceRegistry::restorePersisted(InterfaceEntry &entry, const QString &id) const
{
    const QVariant raw = m_store->data().value(id);
    if (!raw.isValid())
        return;

    const QVariantMap record = raw.toMap();
    entry.alias = record.value(QStringLiteral("alias")).toString();
    entry.hidden = record.value(QStringLiteral("hidden")).toBool();
    entry.settings = record.value(QStringLiteral("settings")).toMap();
}

void InterfaceRegistry::refresh()
{
    QHash<QString, InterfaceDescriptor> seen;
    for (IInterfacePlugin *plugin : m_plugins->plugins()) {
        const QList<InterfaceDescriptor> descriptors = plugin->enumerate();
        for (InterfaceDescriptor descriptor : descriptors) {
            if (!descriptor.isValid()) {
                qCWarning(lcRegistry) << plugin->pluginId() << "returned a descriptor with no id";
                continue;
            }
            // pluginId проставляет реестр, а не плагин: так исключён целый класс ошибок,
            // когда плагин оставляет поле пустым или указывает чужой идентификатор.
            descriptor.pluginId = plugin->pluginId();
            seen.insert(descriptor.id, descriptor);
        }
    }

    QStringList appeared;
    QStringList disappeared;
    bool dirty = false;

    // Появившиеся и уже присутствующие.
    for (auto it = seen.constBegin(); it != seen.constEnd(); ++it) {
        // Устройство, впервые увиденное за этот запуск программы, стоит поискать в
        // interfaces.json: если оно уже когда-то настраивалось, это тот самый момент,
        // чтобы вернуть псевдоним и настройки, пока запись ещё не создана.
        const bool isNewToSession = !m_entries.contains(it.key());
        InterfaceEntry &entry = m_entries[it.key()];
        const bool wasPresent = entry.present;

        entry.descriptor = it.value();

        if (isNewToSession) {
            // Подсказка плагина о скрытии по умолчанию действует только тогда, когда
            // устройство вообще ни разу не сохранялось: у уже сохранённого решение
            // пользователя (ниже, через restorePersisted()) всегда главнее.
            entry.hidden = entry.descriptor.hiddenByDefault;
            restorePersisted(entry, it.key());
        }

        entry.present = true;

        if (!wasPresent) {
            entry.discoveredAt = QDateTime::currentDateTime();
            appeared.append(it.key());
            persist(it.key());
            dirty = true;
        }
    }

    // Пропавшие. Запись остаётся в m_entries до конца сеанса (см. entries()), но из
    // interfaces.json убирается немедленно: недоступное устройство не должно копиться в
    // файле год за годом, а на следующем запуске программы список и так начнётся заново
    // с того, что подключено на самом деле.
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->present && !seen.contains(it.key())) {
            it->present = false;
            it->discoveredAt = {};
            disappeared.append(it.key());
            m_store->remove(it.key());
            dirty = true;
        }
    }

    // Сначала пропажи, потом появления. При замене переходника на другой той же модели
    // обратный порядок дал бы «появилось» раньше «пропало», и сессия закрыла бы только
    // что открытый канал.
    for (const QString &id : std::as_const(disappeared)) {
        qCInfo(lcRegistry) << "lost" << id;
        Q_EMIT interfaceDisappeared(id);
    }
    for (const QString &id : std::as_const(appeared)) {
        qCInfo(lcRegistry) << "found" << id;
        Q_EMIT interfaceAppeared(id);
    }

    if (dirty)
        Q_EMIT changed();
}

QList<InterfaceEntry> InterfaceRegistry::entries() const
{
    QList<InterfaceEntry> result = m_entries.values();

    std::sort(result.begin(), result.end(), [](const InterfaceEntry &a, const InterfaceEntry &b) {
        if (a.present != b.present)
            return a.present; // присутствующие выше
        return a.displayName().localeAwareCompare(b.displayName()) < 0;
    });

    return result;
}

const InterfaceEntry *InterfaceRegistry::entry(const QString &id) const
{
    const auto it = m_entries.constFind(id);
    return it != m_entries.constEnd() ? &*it : nullptr;
}

void InterfaceRegistry::setAlias(const QString &id, const QString &alias)
{
    const auto it = m_entries.find(id);
    if (it == m_entries.end() || it->alias == alias)
        return;

    it->alias = alias;
    persist(id);
    Q_EMIT changed();
}

void InterfaceRegistry::setHidden(const QString &id, bool hidden)
{
    const auto it = m_entries.find(id);
    if (it == m_entries.end() || it->hidden == hidden)
        return;

    it->hidden = hidden;
    persist(id);
    Q_EMIT changed();
}

QVariantMap InterfaceRegistry::settingsFor(const QString &id) const
{
    const InterfaceEntry *entry = this->entry(id);
    if (!entry)
        return {};

    IInterfacePlugin *plugin = m_plugins->plugin(entry->descriptor.pluginId);
    if (!plugin)
        return entry->settings;

    // Нормализация при чтении — то, что позволяет сохранённой конфигурации пережить
    // появление или исчезновение параметра между версиями плагина.
    return plugin->settingsSchema().normalized(entry->settings);
}

void InterfaceRegistry::setSettingsFor(const QString &id, const QVariantMap &settings)
{
    const auto it = m_entries.find(id);
    if (it == m_entries.end() || it->settings == settings)
        return;

    it->settings = settings;
    persist(id);
    Q_EMIT changed();
}

void InterfaceRegistry::persist(const QString &id) const
{
    const InterfaceEntry *entry = this->entry(id);
    if (!entry)
        return;

    QVariantMap record;
    record.insert(QStringLiteral("pluginId"), entry->descriptor.pluginId);
    record.insert(QStringLiteral("systemName"), entry->descriptor.systemName);
    record.insert(QStringLiteral("description"), entry->descriptor.description);
    record.insert(QStringLiteral("alias"), entry->alias);
    record.insert(QStringLiteral("hidden"), entry->hidden);
    record.insert(QStringLiteral("settings"), entry->settings);

    m_store->setValue(id, record);
}

} // namespace spotty
