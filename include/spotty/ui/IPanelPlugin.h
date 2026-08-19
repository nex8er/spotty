/**
 * \file IPanelPlugin.h
 * \brief Точка входа панельного плагина.
 */
#pragma once

#include <spotty/api/IDataFilter.h>
#include <spotty/api/SettingsSchema.h>
#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/PanelDescriptor.h>
#include <spotty/ui/UiApiVersion.h>

#include <QList>
#include <QString>
#include <QtPlugin>

class QWidget;

namespace spotty {

/**
 * \class IPanelPlugin
 * \brief Плагин, добавляющий панели и обработку данных.
 *
 * \par Чем отличается от плагина интерфейса
 *
 * Транспорт отвечает на вопрос «откуда байты». Панельный плагин отвечает на вопросы «что с
 * ними делать» и «как это показать»: график из потока, отправка файла, расшифровка
 * протокола, журнал, макросы. Ему разрешено то, что запрещено транспорту, — линковаться с
 * Qt6::Widgets и создавать виджеты.
 *
 * \par Что обязательно
 *
 * Только pluginId() и displayName(). Плагин без панелей законен и полезен: звено цепочки
 * преобразования может не показывать ничего. Панель без фильтра — обычный случай.
 *
 * \par Правило про главное окно
 *
 * Виджет панели волен обращаться к `window()` как к родителю модального диалога — выбор
 * файла, вопрос с подтверждением. Вешать на него QShortcut или фильтр событий нельзя:
 * сочетания клавиш объявляются через spotty::IPanelHost::setShortcuts(), иначе они
 * переживут саму панель, а раздел настроек о них не узнает.
 */
class IPanelPlugin
{
public:
    virtual ~IPanelPlugin() = default;

    /**
     * \brief Устойчивый идентификатор в нижнем регистре: `"macros"`, `"plotter"`.
     *
     * \warning Входит в пути настроек (`plugins/<pluginId>/…`) и в имя каталога данных.
     *          Переименование обнуляет всё, что пользователь настроил.
     *
     * \note Совпадение с идентификатором плагина интерфейса законно и означает, что один
     *       объект играет обе роли, — тогда у них общий каталог данных. Имя `"plugins"`
     *       занято: каталог с таким именем внутри конфигурации уже принадлежит
     *       установленным пользователем плагинам.
     */
    virtual QString pluginId() const = 0;

    /// \brief Название для пользователя, через tr().
    virtual QString displayName() const = 0;

    /**
     * \brief Панели, которые плагин добавляет в окно.
     *
     * Вызывается один раз при загрузке. Пустой список законен.
     */
    virtual QList<PanelDescriptor> panels() const { return {}; }

    /**
     * \brief Создать виджет панели.
     * \param panelId Идентификатор из panels().
     * \param host Живёт дольше панели; один экземпляр на плагин.
     * \param parent Родитель в дереве виджетов.
     * \return Владение переходит вызывающей стороне; `nullptr` для чужого идентификатора.
     */
    virtual QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
    {
        Q_UNUSED(panelId);
        Q_UNUSED(host);
        Q_UNUSED(parent);
        return nullptr;
    }

    /**
     * \brief Звено цепочки преобразования потока или `nullptr`.
     *
     * Объект обязан жить не меньше плагина: врезка делается один раз при запуске.
     */
    virtual IDataFilter *dataFilter() { return nullptr; }

    /**
     * \brief Место звена в цепочке: меньше — раньше в приёмном направлении.
     *
     * Совпадения разрешаются по pluginId(), а не по порядку загрузки: тот зависит от
     * обхода каталогов и различается от машины к машине.
     */
    virtual int filterOrder() const { return 500; }

    /**
     * \brief Схема настроек для страницы плагина в диалоге настроек.
     *
     * Пустая схема — страницы нет. Значения живут по ключам `plugins/<pluginId>/<key>` и
     * доступны панели через spotty::IPanelHost::value().
     *
     * Для настроек, которые правятся прямо в панели, схема не нужна: она — про то, чему
     * место в общем диалоге.
     */
    virtual SettingsSchema settingsSchema() const { return {}; }

    /**
     * \brief Версия ABI, против которой собран плагин.
     *
     * Переопределять не нужно.
     *
     * \note Метод назван иначе, чем apiVersion() у spotty::IInterfacePlugin, не для
     *       красоты: класс, реализующий обе роли сразу, получил бы два одноимённых чисто
     *       виртуальных метода, и неквалифицированный вызов стал бы неоднозначным.
     */
    virtual int uiApiVersion() const { return SPOTTY_UI_API_VERSION; }
};

} // namespace spotty

Q_DECLARE_INTERFACE(spotty::IPanelPlugin, SPOTTY_PANEL_PLUGIN_IID)
