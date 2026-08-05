/**
 * \file InterfaceLabel.h
 * \brief Как подписывать интерфейс в списках: общая логика для нескольких виджетов.
 */
#pragma once

#include <QString>

namespace spotty {

struct InterfaceEntry;
struct InterfaceDescriptor;
class PluginManager;

/**
 * \brief Подпись интерфейса, не зависящая от псевдонима.
 * \param descriptor Дескриптор устройства.
 * \return Имя, которое сообщил драйвер (плюс серийный номер, если он есть), а если
 *         драйвер вообще ничего не сообщил — системное имя.
 *
 * Используется и как готовая подпись (когда псевдонима нет), и как подсказка-плейсхолдер
 * в поле псевдонима — в обоих случаях это один и тот же «а что было бы по умолчанию».
 */
QString interfaceDefaultName(const InterfaceDescriptor &descriptor);

/**
 * \brief Основная подпись интерфейса для списков: spotty::InterfaceBar и переключатель
 *        устройств в настройках.
 * \param entry Запись реестра.
 * \param plugins Менеджер плагинов; nullptr — выжимку настроек не показывать.
 * \return Псевдоним, если задан, иначе interfaceDefaultName(), плюс выжимка настроек от
 *         плагина-владельца, если тот её вернул.
 */
QString interfacePrimaryLabel(const InterfaceEntry &entry, PluginManager *plugins);

} // namespace spotty
