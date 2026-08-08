/**
 * \file LoggingPlugin.h
 * \brief Панельный плагин записи вывода в файл.
 */
#pragma once

#include <spotty/ui/IPanelPlugin.h>

namespace spotty {

/**
 * \class LoggingPlugin
 * \brief Объявляет панель журналирования и её настройки.
 *
 * Единственный из встроенных, у кого есть settingsSchema(): каталог, шаблон имени и
 * автозапуск правятся редко и в панели были бы лишними, а в общем диалоге настроек —
 * ровно на месте. Флажки «убирать ANSI» и «писать отправленное» продублированы в панели,
 * потому что их переключают по ходу работы.
 */
class LoggingPlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_PANEL_PLUGIN_IID FILE "logging.json")
    Q_INTERFACES(spotty::IPanelPlugin)

public:
    QString pluginId() const override { return QStringLiteral("logging"); }
    QString displayName() const override { return tr("Logging"); }

    QList<PanelDescriptor> panels() const override;
    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override;
    SettingsSchema settingsSchema() const override;
};

} // namespace spotty
