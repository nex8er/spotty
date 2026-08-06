/**
 * \file JlinkDeviceDatabase.h
 * \brief Кэш имён целевых устройств из встроенной базы libjlinkarm для автодополнения.
 */
#pragma once

#include <QStringList>

namespace spotty {

/**
 * \class JlinkDeviceDatabase
 * \brief Имена чипов, которые понимает `ExecCommand("device = ...")` J-Link software.
 *
 * Источник — spotty::JlinkArmLibrary::knownDevices(), то есть сама libjlinkarm, а не
 * JLinkDevices.xml рядом с ней: тот — усечённый список ради загрузчиков флеша (не находил
 * ни STM32F407, ни GD32F450), тогда как встроенная база — тысячи чипов, вкомпилированных
 * в саму библиотеку. Этот класс — только кэш результата (сортировка, снятие повторов):
 * без него пришлось бы заново перебирать ~9000 записей при каждом открытии диалога
 * настроек.
 */
class JlinkDeviceDatabase
{
public:
    /// \return Единственный экземпляр на процесс.
    static JlinkDeviceDatabase &instance();

    /// \return Имена устройств по алфавиту, без повторов. Пустой список, если
    ///         libjlinkarm не найдена.
    const QStringList &deviceNames();

private:
    JlinkDeviceDatabase() = default;

    void load();

    bool m_loaded = false;
    QStringList m_names;
};

} // namespace spotty
