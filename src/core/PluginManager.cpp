/**
 * \file PluginManager.cpp
 * \brief Реализация spotty::PluginManager.
 */
#include "PluginManager.h"

#include "settings/Paths.h"

#include <spotty/api/IInterfacePlugin.h>

#include <QDir>
#include <QLibrary>
#include <QLoggingCategory>
#include <QPluginLoader>

namespace spotty {

/// \brief Категория журналирования: `spotty.plugins`.
Q_LOGGING_CATEGORY(lcPlugins, "spotty.plugins")

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
}

PluginManager::~PluginManager() = default;

void PluginManager::load()
{
    if (m_loaded)
        return;
    m_loaded = true;

    loadStaticPlugins();
    loadDynamicPlugins();

    qCInfo(lcPlugins) << "loaded" << m_plugins.size() << "plugin(s),"
                      << m_failures.size() << "rejected";
}

void PluginManager::loadStaticPlugins()
{
    const QObjectList instances = QPluginLoader::staticInstances();
    for (QObject *instance : instances)
        registerInstance(instance, QStringLiteral("<static>"));
}

void PluginManager::loadDynamicPlugins()
{
    m_searchedDirs = Paths::pluginDirs();

    for (const QString &dirPath : std::as_const(m_searchedDirs)) {
        QDir dir(dirPath);
        const QFileInfoList entries =
            dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

        for (const QFileInfo &entry : entries) {
            const QString path = entry.absoluteFilePath();
            // Отсекает файлы с посторонними расширениями до попытки загрузки, чтобы
            // случайный README в каталоге плагинов не превратился в запись об ошибке.
            if (!QLibrary::isLibrary(path))
                continue;

            QPluginLoader loader(path);
            QObject *instance = loader.instance();
            if (!instance) {
                // В подавляющем большинстве случаев это несовпадение версии Qt или
                // компилятора. Сообщение Qt об этом говорит прямо, поэтому передаём его
                // пользователю целиком, а не заменяем общей фразой.
                m_failures.append({path, loader.errorString()});
                qCWarning(lcPlugins) << "cannot load" << path << loader.errorString();
                continue;
            }

            if (!registerInstance(instance, path))
                loader.unload();
        }
    }
}

bool PluginManager::registerInstance(QObject *instance, const QString &origin)
{
    auto *plugin = qobject_cast<IInterfacePlugin *>(instance);
    if (!plugin) {
        m_failures.append({origin, tr("Not a Spotty interface plugin.")});
        return false;
    }

    if (plugin->apiVersion() != SPOTTY_API_VERSION) {
        const QString reason = tr("Built against API version %1, this build expects %2.")
                                   .arg(plugin->apiVersion())
                                   .arg(SPOTTY_API_VERSION);
        m_failures.append({origin, reason});
        qCWarning(lcPlugins) << origin << reason;
        return false;
    }

    const QString id = plugin->pluginId();
    if (id.isEmpty()) {
        m_failures.append({origin, tr("Plugin reports an empty id.")});
        return false;
    }

    if (IInterfacePlugin *existing = this->plugin(id)) {
        Q_UNUSED(existing);
        // Побеждает найденный раньше, а порядок каталогов задаёт Paths::pluginDirs().
        // Благодаря этому свежую сборку плагина можно положить в пользовательский каталог,
        // и она перекроет штатную, лежащую внутри приложения.
        const QString reason = tr("Another plugin already provides id \"%1\".").arg(id);
        m_failures.append({origin, reason});
        qCWarning(lcPlugins) << origin << reason;
        return false;
    }

    m_plugins.append(plugin);
    qCInfo(lcPlugins) << "loaded" << id << "from" << origin;
    return true;
}

IInterfacePlugin *PluginManager::plugin(const QString &pluginId) const
{
    for (IInterfacePlugin *plugin : m_plugins) {
        if (plugin->pluginId() == pluginId)
            return plugin;
    }
    return nullptr;
}

} // namespace spotty
