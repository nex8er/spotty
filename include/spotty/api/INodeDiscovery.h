/**
 * \file INodeDiscovery.h
 * \brief Необязательная роль транспорта: поиск адресуемых узлов без виджетов.
 */
#pragma once

#include <spotty/api/InterfaceDescriptor.h>
#include <QList>
#include <QVariantMap>
#include <QtPlugin>

namespace spotty {

/// \brief Узел, однажды ответивший на запрос; порядок списка — порядок первых ответов.
struct NetworkNode
{
    int number = 0;
    bool online = false;
};

/// \brief Адаптер с адресуемыми узлами для панельного SDK.
struct NetworkAdapter
{
    QString id;
    QString name;
    int selectedNode = 0;
};

/**
 * \brief Дополнительный интерфейс транспорта, запрашиваемый независимо от основного ABI.
 * \note Все методы вызываются в потоке UI и возвращают накопленное без ожидания железа.
 */
enum class NodeDiscoveryMode { Disconnected, Passive, Active };

class INodeDiscovery
{
public:
    virtual ~INodeDiscovery() = default;
    virtual void scanOnce(const InterfaceDescriptor &descriptor, const QVariantMap &settings) = 0;
    /// \brief Отключение запрещает повторное открытие поиском, включая диалог настроек.
    virtual void setDiscoveryMode(const InterfaceDescriptor &descriptor, NodeDiscoveryMode mode) = 0;
    /// \brief Ключ настройки, выбирающей узел; значения узлов — целые числа.
    virtual QString nodeSettingKey() const = 0;
    /// \brief Снимок узлов; discover включает опрос панели, false снимает её запрос.
    virtual QList<NetworkNode> networkNodes(const InterfaceDescriptor &descriptor,
                                           const QVariantMap &settings, bool discover) = 0;
    /// \brief Забыть порядок и присутствие узлов, сохранив настройки подключения.
    virtual void clearNetworkNodes(const InterfaceDescriptor &descriptor) = 0;
};

} // namespace spotty

Q_DECLARE_INTERFACE(spotty::INodeDiscovery, "org.spotty.INodeDiscovery/3.0")
