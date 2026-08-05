/**
 * \file JlinkDeviceDatabase.h
 * \brief Список имён целевых устройств из JLinkDevices.xml для автодополнения.
 */
#pragma once

#include <QStringList>

namespace spotty {

/**
 * \class JlinkDeviceDatabase
 * \brief Имена чипов, которые понимает `ExecCommand("device = ...")` J-Link software.
 *
 * JLinkDevices.xml — часть того же стороннего J-Link software, что и libjlinkarm (см.
 * spotty::JlinkArmLibrary), и по тем же причинам не читается на этапе сборки: файл
 * читается лениво, при первом обращении, а не найден — значит, поле «Target device»
 * останется обычным полем ввода без подсказок, а не сломает плагин.
 */
class JlinkDeviceDatabase
{
public:
    /// \return Единственный экземпляр на процесс.
    static JlinkDeviceDatabase &instance();

    /// \return Имена устройств из атрибута `Name` в JLinkDevices.xml, по алфавиту, без
    ///         повторов. Пустой список, если файл не найден или не разобрался.
    const QStringList &deviceNames();

private:
    JlinkDeviceDatabase() = default;

    void load();

    bool m_loaded = false;
    QStringList m_names;
};

} // namespace spotty
