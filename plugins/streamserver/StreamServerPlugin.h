/**
 * \file StreamServerPlugin.h
 * \brief Плагин панели доступа к потоку интерфейса для других программ.
 */
#pragma once

#include <spotty/ui/IPanelPlugin.h>

#include <QObject>

namespace spotty {

/**
 * \class StreamServerPlugin
 * \brief Открывает поток интерфейса по локальному сокету или TCP, без протокола.
 *
 * Нужен, чтобы скрипты (bash, Python) читали данные устройства и писали в него, не открывая
 * сам порт: порт занят Spotty, а второй процесс его открыть не может.
 *
 * \see spotty::StreamServer
 */
class StreamServerPlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_PANEL_PLUGIN_IID FILE "streamserver.json")
    Q_INTERFACES(spotty::IPanelPlugin)

public:
    QString pluginId() const override { return QStringLiteral("streamserver"); }
    QString displayName() const override { return tr("Stream server"); }

    QList<PanelDescriptor> panels() const override;
    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override;
};

} // namespace spotty
