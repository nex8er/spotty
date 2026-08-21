/**
 * \file PanelPluginRegistry.h
 * \brief Реестр панельных плагинов.
 */
#pragma once

#include <PluginManager.h>

#include <spotty/ui/IPanelPlugin.h>
#include <spotty/ui/PanelDescriptor.h>

#include <QList>
#include <QString>
#include <QStringList>

namespace spotty {

/**
 * \class PanelPluginRegistry
 * \brief Вторая фаза загрузки: разбор панельной роли.
 *
 * \par Почему не в spotty::PluginManager
 *
 * Панельный интерфейс объявлен в SDK, который линкуется с Qt6::Widgets, а менеджер живёт
 * в `spotty-core`, где виджеты запрещены. Поэтому менеджер обходит каталоги и отдаёт
 * созданные объекты через instances(), а роль разбирается здесь — в слое UI, которому
 * виджеты позволены.
 *
 * \par Выключенные плагины
 *
 * setDisabledPlugins() задаёт идентификаторы, которые не регистрировать. Список тот же,
 * что у spotty::PluginManager, но задаётся отдельно: реестр не должен наследовать чужую
 * политику молча, а собранным без менеджера (addBuiltin()) взять её и вовсе неоткуда.
 *
 * Выключение — не отказ: такой плагин лежит в disabledPanelPlugins(), откуда диалог
 * настроек берёт его имя для флажка обратного включения, а его панели не создаются вовсе.
 *
 * \par Проверки
 *
 * Те же четыре, что у транспортов, плюс пятая:
 * - объект не приводится к spotty::IPanelPlugin — не наша роль, отказом не считается;
 * - собран против другой версии панельного API (#SPOTTY_UI_API_VERSION);
 * - пустой идентификатор плагина;
 * - повтор идентификатора плагина;
 * - повтор идентификатора **панели** — он уникален глобально, а не внутри плагина,
 *   потому что попадает в настройки и служит адресом для activatePanel().
 */
class PanelPluginRegistry
{
    Q_DECLARE_TR_FUNCTIONS(spotty::PanelPluginRegistry)

public:
    /**
     * \struct PanelEntry
     * \brief Панель вместе с плагином, который её объявил.
     */
    struct PanelEntry
    {
        PanelDescriptor descriptor;
        IPanelPlugin *plugin = nullptr;
    };

    /**
     * \param plugins Менеджер, уже прошедший load(). Может быть `nullptr` — тогда реестр
     *        наполняется только через addBuiltin().
     */
    explicit PanelPluginRegistry(PluginManager *plugins = nullptr);

    /**
     * \brief Разобрать панельную роль у всего, что загрузил менеджер.
     *
     * Повторные вызовы игнорируются. Встроенные плагины, добавленные через addBuiltin() до
     * этого момента, остаются на своих местах и участвуют в проверке повторов.
     */
    void load();

    /**
     * \brief Добавить плагин, вкомпилированный в приложение напрямую.
     * \return `false`, если плагин отклонён или выключен; причина отказа добавляется в
     *         failures(), выключение — нет: это не отказ.
     *
     * Минует менеджер: встроенной панели незачем притворяться загруженной из файла.
     * Проверки выполняются те же.
     */
    bool addBuiltin(IPanelPlugin *plugin,
                    const QString &origin = QStringLiteral("<builtin>"));

    /**
     * \brief Задать плагины, которые не регистрировать.
     * \param pluginIds Значения IPanelPlugin::pluginId(), которые нужно пропустить.
     *
     * \note Вызывать до load() и до addBuiltin(): позже список ни на что не влияет.
     */
    void setDisabledPlugins(const QStringList &pluginIds) { m_disabledIds = pluginIds; }

    /// \return Идентификаторы, переданные в setDisabledPlugins().
    const QStringList &disabledPlugins() const { return m_disabledIds; }

    /// \return Панельные плагины, пропущенные как выключенные. Их панели не создавались.
    const QList<IPanelPlugin *> &disabledPanelPlugins() const { return m_disabled; }

    const QList<IPanelPlugin *> &plugins() const { return m_plugins; }

    /// \return Все панели всех плагинов, упорядоченные по паре (order, id).
    const QList<PanelEntry> &panels() const { return m_panels; }

    /// \return Панель по идентификатору или `nullptr`.
    const PanelEntry *panel(const QString &panelId) const;

    /// \return Плагин по идентификатору или `nullptr`.
    IPanelPlugin *plugin(const QString &pluginId) const;

    const QList<PluginManager::LoadFailure> &failures() const { return m_failures; }

private:
    /// \brief Проверить плагин и разложить его панели по местам.
    bool registerPlugin(IPanelPlugin *plugin, const QString &origin);

    /// \return Выключенный плагин по идентификатору или `nullptr`.
    IPanelPlugin *disabledPlugin(const QString &pluginId) const;

    /// \brief Упорядочить m_panels по (order, id).
    void sortPanels();

    PluginManager *m_manager = nullptr;
    QList<IPanelPlugin *> m_plugins;
    QList<IPanelPlugin *> m_disabled; ///< Выключенные пользователем.
    QStringList m_disabledIds;        ///< Идентификаторы выключенных.
    QList<PanelEntry> m_panels;
    QList<PluginManager::LoadFailure> m_failures;
    bool m_loaded = false;
};

} // namespace spotty
