/**
 * \file JsonTreePlugin.h
 * \brief Плагин панели разбора JSON.
 */
#pragma once

#include <spotty/data/JsonFramer.h>
#include <spotty/ui/IPanelPlugin.h>

#include <QObject>

namespace spotty {

class JsonTreeModel;

/**
 * \class JsonTreePlugin
 * \brief Читает поток терминала, собирает документы JSON и раскладывает их в дерево.
 *
 * \par Модель и фреймер принадлежат плагину, а не панели
 *
 * Панель закрывают, а накопление должно продолжаться: вернувшись к панели, человек обязан
 * увидеть дерево, а не пустой экран. Та же причина, что у spotty::PlotterPlugin.
 *
 * \par Настройки применяет плагин, а не панель
 *
 * Настройки правят модель, а модель живёт дольше панели и существует, даже когда панель ни
 * разу не открывали. Если бы их читала панель, поток до её первого открытия разбирался бы с
 * умолчаниями — записанная ловушка про панель, читающую настройки в конструкторе.
 */
class JsonTreePlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_PANEL_PLUGIN_IID FILE "jsontree.json")
    Q_INTERFACES(spotty::IPanelPlugin)

public:
    JsonTreePlugin();
    ~JsonTreePlugin() override;

    /// \copydoc spotty::IPanelPlugin::pluginId
    QString pluginId() const override { return QStringLiteral("jsontree"); }

    /// \copydoc spotty::IPanelPlugin::displayName
    QString displayName() const override { return tr("JSON tree"); }

    /// \copydoc spotty::IPanelPlugin::panels
    QList<PanelDescriptor> panels() const override;

    /// \copydoc spotty::IPanelPlugin::createPanel
    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override;

    /// \copydoc spotty::IPanelPlugin::settingsSchema
    SettingsSchema settingsSchema() const override;

private:
    /// \brief Перечитать настройки в модель и фреймер.
    void applySettings();

    JsonTreeModel *m_model = nullptr;
    JsonFramer m_framer;

    IPanelHost *m_host = nullptr;

    /// \brief Докуда разобран буфер терминала.
    qint64 m_nextLine = 0;

    /// \brief Пауза разбора; ставится панелью.
    bool m_paused = false;
};

} // namespace spotty
