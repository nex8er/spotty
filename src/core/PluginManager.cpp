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

#include <algorithm>

namespace spotty {

/// \brief Категория журналирования: `spotty.plugins`.
Q_LOGGING_CATEGORY(lcPlugins, "spotty.plugins")

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
}

PluginManager::~PluginManager() = default;

void PluginManager::setDisabledPlugins(const QStringList &pluginIds)
{
    m_disabledIds = pluginIds;
}

void PluginManager::load()
{
    if (m_loaded)
        return;
    m_loaded = true;

    loadStaticPlugins();
    loadDynamicPlugins();

    qCInfo(lcPlugins) << "found" << m_instances.size() << "instance(s),"
                      << m_plugins.size() << "interface plugin(s),"
                      << m_disabled.size() << "disabled";
}

void PluginManager::markRecognized(QObject *instance)
{
    for (LoadedInstance &loaded : m_instances) {
        if (loaded.instance == instance) {
            loaded.recognized = true;
            return;
        }
    }
}

void PluginManager::finishLoading()
{
    if (m_finished)
        return;
    m_finished = true;

    for (const LoadedInstance &loaded : std::as_const(m_instances)) {
        if (loaded.recognized)
            continue;
        // Библиотека загрузилась, объект создался, но ни один реестр ролей его не принял.
        // Почти всегда это плагин от другой версии Spotty либо чужой плагин Qt, случайно
        // положенный в каталог.
        const QString reason = tr("Not a Spotty plugin of any known kind.");
        m_failures.append({loaded.origin, reason});
        qCWarning(lcPlugins) << loaded.origin << reason;
    }

    qCInfo(lcPlugins) << "loaded" << m_plugins.size() << "interface plugin(s),"
                      << m_failures.size() << "rejected";
}

void PluginManager::loadStaticPlugins()
{
    const QObjectList instances = QPluginLoader::staticInstances();
    for (QObject *instance : instances) {
        m_instances.append({instance, QStringLiteral("<static>"), false});
        registerInstance(instance, QStringLiteral("<static>"), /*deferUnrecognized=*/true);
    }
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

            // Выгрузить нельзя, даже если наша роль не подошла: роль объекта к этому
            // моменту разобрана не полностью — панельный реестр к нему ещё не подходил, —
            // а выгрузка библиотеки из-под живого QObject это гарантированное падение.
            // Отображённая в память чужая библиотека дешевле.
            m_instances.append({instance, path, false});
            registerInstance(instance, path, /*deferUnrecognized=*/true);
        }
    }
}

bool PluginManager::addPlugin(QObject *instance, const QString &origin)
{
    // Запись заводится до проверок, чтобы markRecognized() изнутри registerInstance()
    // нашёл её и пометка легла на тот же экземпляр.
    m_instances.append({instance, origin, false});
    return registerInstance(instance, origin, /*deferUnrecognized=*/false);
}

bool PluginManager::registerInstance(QObject *instance, const QString &origin,
                                     bool deferUnrecognized)
{
    auto *plugin = qobject_cast<IInterfacePlugin *>(instance);
    if (!plugin) {
        if (!deferUnrecognized) {
            // Вызывающий передал конкретный экземпляр и ждёт приговора сейчас, а не после
            // разбора чужих ролей. Приговор окончательный, поэтому запись помечается
            // разобранной: finishLoading() не должен добавить к ней вторую.
            m_failures.append({origin, tr("Not a Spotty interface plugin.")});
            markRecognized(instance);
        }
        return false;
    }

    // Роль признана — дальше идут проверки самого плагина. Пометка ставится здесь, а не
    // после них: провал по версии или по повтору идентификатора уже даёт свою запись в
    // failures(), и finishLoading() не должен добавлять к ней вторую, гораздо менее
    // внятную. «Распознан» значит «роль ясна», а не «проверки пройдены».
    markRecognized(instance);

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

    if (m_disabledIds.contains(id)) {
        // Не отказ и не ошибка: пользователь выключил плагин сам. В failures() ему не
        // место — там ищут причину, по которой плагин пропал сам собой, и выключенное
        // вручную только мешало бы. Объект сохраняется, чтобы диалогу настроек было чем
        // подписать флажок для обратного включения.
        //
        // Тот же плагин может лежать в двух каталогах сразу — включённому второй экземпляр
        // отсекает проверка повтора ниже, до которой выключенный не доходит. Без этой
        // проверки в диалоге настроек появилась бы вторая строка с тем же флажком.
        const bool known = std::any_of(m_disabled.cbegin(), m_disabled.cend(),
                                       [&id](IInterfacePlugin *other) {
                                           return other->pluginId() == id;
                                       });
        if (!known)
            m_disabled.append(plugin);

        qCInfo(lcPlugins) << "disabled" << id << "from" << origin;
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
