/**
 * \file InterfaceRegistry.cpp
 * \brief Реализация spotty::InterfaceRegistry.
 */
#include "InterfaceRegistry.h"

#include "InterfaceEnumerationWorker.h"
#include "PluginManager.h"
#include "settings/SettingsStore.h"

#include <spotty/api/IInterfacePlugin.h>

#include <QLoggingCategory>
#include <QThread>
#include <QTimer>
#include <QUuid>

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

InterfaceRegistry::~InterfaceRegistry()
{
    if (m_pollThread) {
        // wait() без таймаута: worker не держит ничего, что не освободилось бы само —
        // максимум придётся дождаться, пока сторонний enumerate() вернётся из уже
        // идущего вызова, прервать который всё равно нечем.
        m_pollThread->quit();
        m_pollThread->wait();
        delete m_pollThread;
    }
}

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
        connect(m_pollTimer, &QTimer::timeout, this, &InterfaceRegistry::pollAsync);
        m_pollTimer->start();
    }

    // Устройства пользователя читаются до первого обхода: перечислять их некому, и без этого
    // шага они появились бы в списке лишь после первого же изменения настроек.
    restoreUserDevices();

    // Самый первый обход — синхронно и напрямую через refresh(), не через pollAsync():
    // вызывающая сторона (MainWindow) должна увидеть уже подключённые устройства сразу по
    // возврату из start(), а не через неопределённое время после того, как отработает
    // фоновый поток. Разовая цена одного медленного плагина на старте программы того
    // стоит — периодический опрос после неё уже уходит в фон.
    refresh();
}

void InterfaceRegistry::deferPolling(int quietPeriodMs)
{
    // Если все плагины дают уведомления сами, останавливать нечего и лишний таймер не
    // нужен. Это также сохраняет прежнее поведение явного refresh() от hotplugNotifier().
    if (!m_pollTimer)
        return;

    m_pollTimer->stop();
    if (!m_pollResumeTimer) {
        m_pollResumeTimer = new QTimer(this);
        m_pollResumeTimer->setSingleShot(true);
        connect(m_pollResumeTimer, &QTimer::timeout, this, [this] {
            pollAsync();
            m_pollTimer->start();
        });
    }

    // start() у однократного таймера переносит срок. Пока пользователь крутит колесо,
    // enumerate() не попадает между кадрами; остановился — состояние устройств тут же
    // актуализируется, без ожидания следующей полной секунды.
    m_pollResumeTimer->start(qMax(0, quietPeriodMs));
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

void InterfaceRegistry::restoreUserDevices()
{
    const QVariantMap stored = m_store->data();
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        const QVariantMap record = it.value().toMap();
        if (!record.value(QStringLiteral("user")).toBool())
            continue;

        IInterfacePlugin *plugin = m_plugins->plugin(record.value(QStringLiteral("pluginId")).toString());
        if (!plugin || !plugin->supportsUserDevices())
            continue;

        InterfaceEntry &entry = m_entries[it.key()];
        restorePersisted(entry, it.key());
        entry.userDefined = true;
        entry.present = true;
        entry.descriptor = describeUserDevice(plugin, it.key(), entry.settings);
    }
}

InterfaceDescriptor InterfaceRegistry::describeUserDevice(IInterfacePlugin *plugin,
                                                          const QString &id,
                                                          const QVariantMap &settings) const
{
    // Плагину отдаётся нормализованное: сохранённое прежней версией могло не знать нового
    // ключа, а описание не должно из-за этого строиться на пустом месте.
    InterfaceDescriptor descriptor =
        plugin->describeUserDevice(plugin->settingsSchema().normalized(settings));
    descriptor.id = id;
    descriptor.pluginId = plugin->pluginId();
    if (descriptor.systemName.isEmpty())
        descriptor.systemName = plugin->displayName();
    return descriptor;
}

QString InterfaceRegistry::addUserDevice(const QString &pluginId)
{
    IInterfacePlugin *plugin = m_plugins->plugin(pluginId);
    if (!plugin || !plugin->supportsUserDevices())
        return {};

    // Случайный идентификатор, а не выведенный из адреса: адрес пользователь правит, а
    // псевдоним, выбор и сохранённое в файле должны остаться за тем же устройством.
    const QString id = QStringLiteral("%1:user:%2")
                           .arg(pluginId, QUuid::createUuid().toString(QUuid::Id128).left(8));

    InterfaceEntry &entry = m_entries[id];
    entry.userDefined = true;
    entry.present = true;
    entry.settings = plugin->settingsSchema().defaults();
    entry.descriptor = describeUserDevice(plugin, id, entry.settings);
    persist(id);

    qCInfo(lcRegistry) << "added user device" << id;
    Q_EMIT interfaceAppeared(id);
    Q_EMIT changed();
    return id;
}

void InterfaceRegistry::removeUserDevice(const QString &id)
{
    const auto it = m_entries.find(id);
    if (it == m_entries.end() || !it->userDefined)
        return;

    m_entries.erase(it);
    m_store->remove(id);

    qCInfo(lcRegistry) << "removed user device" << id;
    Q_EMIT interfaceDisappeared(id);
    Q_EMIT changed();
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
    applyDiscovered(seen);
}

void InterfaceRegistry::pollAsync()
{
    if (m_scanInFlight) {
        // Предыдущий обход ещё не вернулся — новый запрос не запускает второй проход
        // параллельно (второй вызов enumerate() того же PCAN-канала конкурентно с первым
        // ничем хорошим не кончится), а просто помечает, что нужно повторить сразу вслед
        // за текущим.
        m_rescanRequested = true;
        return;
    }

    if (!m_pollThread) {
        // Поток заводится лениво, при первом обращении: платить за него должны только те
        // сборки, где вообще есть плагин без уведомителя устройств (иначе pollAsync()
        // никогда не вызывается).
        m_pollThread = new QThread;
        m_pollThread->setObjectName(QStringLiteral("spotty-enum"));

        m_worker = new InterfaceEnumerationWorker;
        m_worker->setPlugins(m_plugins->plugins());
        m_worker->moveToThread(m_pollThread);
        connect(m_pollThread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &InterfaceEnumerationWorker::finished,
                this, &InterfaceRegistry::onEnumerationFinished);

        m_pollThread->start();
    }

    m_scanInFlight = true;
    QMetaObject::invokeMethod(m_worker, &InterfaceEnumerationWorker::run, Qt::QueuedConnection);
}

void InterfaceRegistry::onEnumerationFinished(const QList<InterfaceDescriptor> &descriptors)
{
    m_scanInFlight = false;

    QHash<QString, InterfaceDescriptor> seen;
    for (const InterfaceDescriptor &descriptor : descriptors) {
        if (!descriptor.isValid()) {
            qCWarning(lcRegistry) << descriptor.pluginId << "returned a descriptor with no id";
            continue;
        }
        seen.insert(descriptor.id, descriptor);
    }
    applyDiscovered(seen);

    if (m_rescanRequested) {
        m_rescanRequested = false;
        pollAsync();
    }
}

void InterfaceRegistry::applyDiscovered(const QHash<QString, InterfaceDescriptor> &seen)
{
    // Захватывается до первого изменения состояния этим самым вызовом: появления,
    // найденные НИЖЕ, должны узнать, что они — самое первое обнаружение программы, а не
    // отличить его от собственной же правки m_firstRefreshDone.
    const bool isFirstRefresh = !m_firstRefreshDone;
    m_firstRefreshDone = true;

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
            // На самом первом опросе «появившееся» на деле уже стояло в системе — просто
            // Spotty только что запустился и увидел это впервые. Когда оно было подключено
            // на самом деле, неизвестно, и оставлять discoveredAt недействительным честнее,
            // чем подставлять текущее время (см. \note у InterfaceEntry::discoveredAt).
            if (!isFirstRefresh)
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
        // Устройство пользователя отсутствует в enumerate() всегда и по замыслу: оно не
        // пропало, его просто некому перечислять.
        if (it->userDefined)
            continue;
        if (it->present && !seen.contains(it.key())) {
            it->present = false;
            it->discoveredAt = {};
            disappeared.append(it.key());
            m_store->remove(it.key());
            dirty = true;
        }
    }

    // Подпись устройства пользователя выводится из настроек и из того, что плагин успел узнать о
    // машине (например, имя хоста, которое приходит по ssh уже после ввода адреса). Поэтому она
    // пересчитывается при каждом обходе, а не только при правке настроек.
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (!it->userDefined)
            continue;
        IInterfacePlugin *plugin = m_plugins->plugin(it->descriptor.pluginId);
        if (!plugin)
            continue;
        const InterfaceDescriptor fresh = describeUserDevice(plugin, it.key(), it->settings);
        if (fresh.systemName != it->descriptor.systemName || fresh.description != it->descriptor.description
            || fresh.extra != it->descriptor.extra) {
            it->descriptor = fresh;
            persist(it.key());
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
    if (it->userDefined) {
        // Подпись и адрес устройства пользователя выводятся из настроек (хост, порт), и
        // после правки список должен показывать уже новые.
        if (IInterfacePlugin *plugin = m_plugins->plugin(it->descriptor.pluginId))
            it->descriptor = describeUserDevice(plugin, id, settings);
    }
    persist(id);
    Q_EMIT changed();
}

void InterfaceRegistry::resetAll()
{
    QStringList removedUserDevices;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->userDefined) {
            removedUserDevices.append(it.key());
            it = m_entries.erase(it);
            continue;
        }
        it->alias.clear();
        it->hidden = it->descriptor.hiddenByDefault;
        it->settings.clear();
        ++it;
    }

    m_store->clear();
    for (const QString &id : std::as_const(removedUserDevices))
        Q_EMIT interfaceDisappeared(id);
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
    if (entry->userDefined)
        record.insert(QStringLiteral("user"), true);

    // Пароли в файл не идут: это единственное место, где настройки покидают память, и
    // решает, что секретно, схема плагина, а не вызывающий. Плагин, которого уже нет,
    // секретов не объявлял — сохраняется как есть.
    IInterfacePlugin *plugin = m_plugins->plugin(entry->descriptor.pluginId);
    record.insert(QStringLiteral("settings"),
                  plugin ? plugin->settingsSchema().persistent(entry->settings) : entry->settings);

    m_store->setValue(id, record);
}

} // namespace spotty
