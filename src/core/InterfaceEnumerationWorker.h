/**
 * \file InterfaceEnumerationWorker.h
 * \brief Фоновый опрос IInterfacePlugin::enumerate() для InterfaceRegistry.
 */
#pragma once

#include <spotty/api/InterfaceDescriptor.h>

#include <QList>
#include <QObject>

namespace spotty {

class IInterfacePlugin;

/**
 * \class InterfaceEnumerationWorker
 * \brief Зовёт enumerate() всех плагинов интерфейсов в своём потоке.
 *
 * \par Почему отдельный поток
 *
 * Контракт SDK требует, чтобы enumerate() был дешёвым, но не может этого гарантировать —
 * это сторонний код. На практике этого недостаточно: официальный Windows-драйвер PEAK
 * PCAN-Basic отвечает на `CAN_GetValue(..., PCAN_CHANNEL_CONDITION, ...)` на порядки
 * медленнее, чем измерено для другого драйвера на macOS (см. \note у
 * PcanLibrary::availableChannels()) — до нескольких секунд на один обход восьми ручек.
 * Раньше весь перебор плагинов шёл в потоке UI по таймеру раз в секунду и замораживал
 * прокрутку терминала на то же время. Здесь — тот же перебор, тот же список плагинов, но
 * не в потоке, который рисует окно.
 *
 * \par Владение плагинами
 *
 * Список плагинов принадлежит spotty::PluginManager и не меняется после
 * PluginManager::finishLoading() (плагины не выгружаются, см. \note у PluginManager) —
 * читать его из чужого потока безопасно без дополнительной синхронизации.
 */
class InterfaceEnumerationWorker : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    /// \brief Список плагинов для опроса. Звать до moveToThread(), пока объект ещё здесь.
    void setPlugins(const QList<IInterfacePlugin *> &plugins) { m_plugins = plugins; }

public Q_SLOTS:
    /// \brief Опросить все плагины и сообщить результат finished().
    void run();

Q_SIGNALS:
    /**
     * \brief Дескрипторы всех найденных устройств, независимо от плагина.
     *
     * Плоский список, а не хэш по InterfaceRegistry::m_entries: сборка diff'а — дело
     * потока UI, здесь только сбор сырых данных.
     */
    void finished(const QList<spotty::InterfaceDescriptor> &descriptors);

private:
    QList<IInterfacePlugin *> m_plugins;
};

} // namespace spotty
