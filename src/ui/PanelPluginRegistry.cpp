/**
 * \file PanelPluginRegistry.cpp
 * \brief Реализация spotty::PanelPluginRegistry.
 */
#include "PanelPluginRegistry.h"

#include <QLoggingCategory>

#include <algorithm>

namespace spotty {

namespace {
Q_LOGGING_CATEGORY(lcPanels, "spotty.panels")
}

PanelPluginRegistry::PanelPluginRegistry(PluginManager *plugins)
    : m_manager(plugins)
{
}

void PanelPluginRegistry::load()
{
    if (m_loaded)
        return;
    m_loaded = true;

    if (!m_manager)
        return;

    for (const PluginManager::LoadedInstance &loaded : m_manager->instances()) {
        auto *plugin = qobject_cast<IPanelPlugin *>(loaded.instance);
        if (!plugin)
            continue; // Чужая роль — не наше дело и не отказ.

        // Роль признана до проверок: провал по версии или по повтору уже даёт свою запись
        // в failures(), и PluginManager::finishLoading() не должен добавить к ней вторую.
        m_manager->markRecognized(loaded.instance);
        registerPlugin(plugin, loaded.origin);
    }

    qCInfo(lcPanels) << "loaded" << m_plugins.size() << "panel plugin(s),"
                     << m_panels.size() << "panel(s)," << m_disabled.size() << "disabled,"
                     << m_failures.size() << "rejected";
}

bool PanelPluginRegistry::addBuiltin(IPanelPlugin *plugin, const QString &origin)
{
    return plugin && registerPlugin(plugin, origin);
}

bool PanelPluginRegistry::registerPlugin(IPanelPlugin *plugin, const QString &origin)
{
    if (plugin->uiApiVersion() != SPOTTY_UI_API_VERSION) {
        const QString reason = tr("Built against panel API version %1, this build expects %2.")
                                   .arg(plugin->uiApiVersion())
                                   .arg(SPOTTY_UI_API_VERSION);
        m_failures.append({origin, reason});
        qCWarning(lcPanels) << origin << reason;
        return false;
    }

    const QString id = plugin->pluginId();
    if (id.isEmpty()) {
        m_failures.append({origin, tr("Panel plugin reports an empty id.")});
        return false;
    }

    if (m_disabledIds.contains(id)) {
        // Пользователь выключил панель сам — в failures() этому не место, там ищут
        // причину, по которой плагин пропал без спросу. Объект сохраняется: диалогу
        // настроек нужно чем-то подписать флажок обратного включения. Проверка повтора
        // своя: до общей, что ниже, выключенный плагин не доходит, а лежать в двух
        // каталогах сразу он может — и дал бы в диалоге две строки с одним флажком.
        if (!disabledPlugin(id))
            m_disabled.append(plugin);

        qCInfo(lcPanels) << "disabled" << id << "from" << origin;
        return false;
    }

    if (this->plugin(id)) {
        // Побеждает найденный раньше — то же правило, что у транспортов.
        const QString reason = tr("Another panel plugin already provides id \"%1\".").arg(id);
        m_failures.append({origin, reason});
        qCWarning(lcPanels) << origin << reason;
        return false;
    }

    // Панели собираются во временный список: плагин с повторяющимся идентификатором
    // панели отклоняется целиком, а не наполовину. Половина зарегистрированных панелей
    // хуже, чем ни одной, — плагин окажется в непредусмотренном им состоянии.
    QList<PanelEntry> pending;
    for (const PanelDescriptor &descriptor : plugin->panels()) {
        if (descriptor.id.isEmpty()) {
            const QString reason = tr("Plugin \"%1\" declares a panel with an empty id.").arg(id);
            m_failures.append({origin, reason});
            qCWarning(lcPanels) << origin << reason;
            return false;
        }

        const bool taken = panel(descriptor.id)
            || std::any_of(pending.cbegin(), pending.cend(), [&descriptor](const PanelEntry &e) {
                   return e.descriptor.id == descriptor.id;
               });
        if (taken) {
            const QString reason =
                tr("Panel id \"%1\" is already taken.").arg(descriptor.id);
            m_failures.append({origin, reason});
            qCWarning(lcPanels) << origin << reason;
            return false;
        }

        pending.append({descriptor, plugin});
    }

    m_plugins.append(plugin);
    m_panels += pending;
    sortPanels();

    qCInfo(lcPanels) << "loaded" << id << "with" << pending.size() << "panel(s) from" << origin;
    return true;
}

IPanelPlugin *PanelPluginRegistry::disabledPlugin(const QString &pluginId) const
{
    for (IPanelPlugin *plugin : m_disabled) {
        if (plugin->pluginId() == pluginId)
            return plugin;
    }
    return nullptr;
}

void PanelPluginRegistry::sortPanels()
{
    // Порядок задают число и идентификатор, а не очерёдность загрузки: та зависит от
    // обхода каталогов и различалась бы от машины к машине.
    std::stable_sort(m_panels.begin(), m_panels.end(),
                     [](const PanelEntry &lhs, const PanelEntry &rhs) {
                         if (lhs.descriptor.order != rhs.descriptor.order)
                             return lhs.descriptor.order < rhs.descriptor.order;
                         return lhs.descriptor.id < rhs.descriptor.id;
                     });
}

const PanelPluginRegistry::PanelEntry *PanelPluginRegistry::panel(const QString &panelId) const
{
    for (const PanelEntry &entry : m_panels) {
        if (entry.descriptor.id == panelId)
            return &entry;
    }
    return nullptr;
}

IPanelPlugin *PanelPluginRegistry::plugin(const QString &pluginId) const
{
    for (IPanelPlugin *plugin : m_plugins) {
        if (plugin->pluginId() == pluginId)
            return plugin;
    }
    return nullptr;
}

} // namespace spotty
