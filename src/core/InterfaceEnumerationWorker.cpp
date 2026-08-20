/**
 * \file InterfaceEnumerationWorker.cpp
 * \brief Реализация spotty::InterfaceEnumerationWorker.
 */
#include "InterfaceEnumerationWorker.h"

#include <spotty/api/IInterfacePlugin.h>

#include <utility>

namespace spotty {

void InterfaceEnumerationWorker::run()
{
    // Проверка isValid() и предупреждение в журнал остаются в InterfaceRegistry: это
    // разбор результата, а не его сбор, и там же живёт категория журналирования.
    QList<InterfaceDescriptor> result;
    for (IInterfacePlugin *plugin : std::as_const(m_plugins)) {
        const QList<InterfaceDescriptor> descriptors = plugin->enumerate();
        for (InterfaceDescriptor descriptor : descriptors) {
            // pluginId проставляется здесь же, а не в InterfaceRegistry: сборщик diff'а
            // получает уже готовые дескрипторы и не должен помнить, какой плагин какой
            // из них вернул.
            descriptor.pluginId = plugin->pluginId();
            result.append(descriptor);
        }
    }
    Q_EMIT finished(result);
}

} // namespace spotty
