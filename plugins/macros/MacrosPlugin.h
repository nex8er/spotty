/**
 * \file MacrosPlugin.h
 * \brief Панельный плагин макросов.
 */
#pragma once

#include <spotty/ui/IPanelPlugin.h>

namespace spotty {

/**
 * \class MacrosPlugin
 * \brief Объявляет панель макросов.
 *
 * \note Идентификатор `"macros"` выбран не произвольно: из него хост составляет каталог
 *       данных `<конфигурация>/macros`, а это ровно тот каталог, в котором пресеты лежали
 *       и раньше. Переименование плагина увело бы файлы пользователя.
 */
class MacrosPlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_PANEL_PLUGIN_IID FILE "macros.json")
    Q_INTERFACES(spotty::IPanelPlugin)

public:
    QString pluginId() const override { return QStringLiteral("macros"); }
    QString displayName() const override { return tr("Macros"); }

    QList<PanelDescriptor> panels() const override;
    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override;
};

} // namespace spotty
