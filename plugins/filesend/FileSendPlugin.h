/**
 * \file FileSendPlugin.h
 * \brief Плагин отправки файла в интерфейс.
 */
#pragma once

#include <spotty/ui/IPanelPlugin.h>

namespace spotty {

/**
 * \class FileSendPlugin
 * \brief Объявляет панель отправки файла.
 *
 * \par Что он показывает про API
 *
 * Передающий путь целиком: выбор файла модальным диалогом, перекодирование готовым
 * spotty::DataCodec из SDK, отправка порциями, прогресс, отмена и корректное поведение при
 * обрыве связи.
 *
 * \par Что он вскрыл
 *
 * У spotty::IPanelHost::send() нет обратного давления: вызов не блокирует, а очередь
 * потока ввода-вывода не ограничена. Единственное, на что можно опереться, — подтверждение
 * записи в dataLogged(). Оно работает, но подтверждает запись в канал, а не приём
 * устройством, и это неявный договор, а не свойство API. Если его окажется мало,
 * следующим шагом станет `bytesPending()` или сигнал об опустевшей очереди.
 */
class FileSendPlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_PANEL_PLUGIN_IID FILE "filesend.json")
    Q_INTERFACES(spotty::IPanelPlugin)

public:
    QString pluginId() const override { return QStringLiteral("filesend"); }
    QString displayName() const override { return tr("Send file"); }

    QList<PanelDescriptor> panels() const override;
    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override;
};

} // namespace spotty
