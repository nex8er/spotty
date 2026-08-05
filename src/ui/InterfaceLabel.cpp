/**
 * \file InterfaceLabel.cpp
 * \brief Реализация spotty::interfaceDefaultName() и spotty::interfacePrimaryLabel().
 */
#include "InterfaceLabel.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <spotty/api/IInterfacePlugin.h>
#include <spotty/api/InterfaceDescriptor.h>

namespace spotty {

QString interfaceDefaultName(const InterfaceDescriptor &descriptor)
{
    // Имя от драйвера («JLink CP2102») читается куда понятнее системного адреса
    // («cu.usbserial-1420», который к тому же переезжает при переподключении). К
    // системному имени опускаемся, только если драйвер вообще ничего не сообщил.
    QString name = descriptor.description.isEmpty() ? descriptor.systemName
                                                     : descriptor.description;

    // Серийный номер отличает два одинаковых переходника друг от друга без похода в
    // настройки каждого.
    if (!descriptor.serialNumber.isEmpty())
        name += QStringLiteral("  ·  ") + descriptor.serialNumber;

    return name;
}

QString interfacePrimaryLabel(const InterfaceEntry &entry, PluginManager *plugins)
{
    QString label = entry.alias.isEmpty() ? interfaceDefaultName(entry.descriptor) : entry.alias;

    // Выжимку настроек составляет плагин: какие параметры существенны и как они читаются
    // вместе — знание транспорта, ядру недоступное.
    if (plugins) {
        if (IInterfacePlugin *plugin = plugins->plugin(entry.descriptor.pluginId)) {
            const QString summary = plugin->settingsSummary(entry.settings);
            if (!summary.isEmpty())
                label += QStringLiteral("  ·  ") + summary;
        }
    }

    return label;
}

} // namespace spotty
