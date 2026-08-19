/**
 * \file IInterfacePlugin.h
 * \brief Точка входа плагина интерфейса.
 */
#pragma once

#include <spotty/api/ApiVersion.h>
#include <spotty/api/IInterfaceChannel.h>
#include <spotty/api/InterfaceDescriptor.h>
#include <spotty/api/SettingsSchema.h>

#include <QList>
#include <QString>
#include <QtPlugin>

namespace spotty {

/**
 * \class IInterfacePlugin
 * \brief Точка входа плагина: перечисляет устройства и создаёт для них каналы.
 *
 * Минимальный плагин реализует пять методов: pluginId(), displayName(), enumerate(),
 * settingsSchema() и createChannel().
 *
 * \par Как объявить плагин
 *
 * \code
 * class UartPlugin : public QObject, public IInterfacePlugin
 * {
 *     Q_OBJECT
 *     Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "uart.json")
 *     Q_INTERFACES(spotty::IInterfacePlugin)
 * public:
 *     QString pluginId() const override { return QStringLiteral("uart"); }
 *     // ...
 * };
 * \endcode
 *
 * И один вызов в `CMakeLists.txt` рядом с исходниками:
 *
 * \code{.cmake}
 * spotty_add_plugin(spotty-plugin-uart
 *     CLASS_NAME UartPlugin
 *     SOURCES UartPlugin.cpp UartPlugin.h UartChannel.cpp UartChannel.h
 *     LINK Qt6::SerialPort)
 * \endcode
 *
 * \note Метода `defaultSettings()` здесь нет намеренно: значения по умолчанию живут в
 *       полях схемы, а ядро берёт их через SettingsSchema::defaults(). Так у каждого
 *       умолчания ровно одно место объявления.
 *
 * \see docs/PLUGIN_API.md — пошаговое руководство.
 */
class IInterfacePlugin
{
public:
    virtual ~IInterfacePlugin() = default;

    /**
     * \brief Устойчивый идентификатор плагина.
     * \return Строку в нижнем регистре, пригодную для имени файла: `"uart"`, `"tcp"`.
     *
     * \warning Составляет первую половину сохраняемых идентификаторов интерфейсов,
     *          поэтому переименование обнуляет все сохранённые пользователем настройки
     *          устройств этого плагина.
     */
    virtual QString pluginId() const = 0;

    /// \brief Название для показа пользователю, через tr(): `tr("Serial / UART")`.
    virtual QString displayName() const = 0;

    /**
     * \brief Устройства, видимые в системе прямо сейчас.
     *
     * \warning Вызывается в потоке UI примерно раз в секунду, поэтому обязан быть
     *          дешёвым и неблокирующим. Если перечисление дорогое (обход сети, опрос
     *          шины), результат нужно кэшировать и обновлять из hotplugNotifier().
     */
    virtual QList<InterfaceDescriptor> enumerate() const = 0;

    /// \brief Описание настроек интерфейса. Диалог строит и значения хранит ядро.
    virtual SettingsSchema settingsSchema() const = 0;

    /**
     * \brief Однострочная выжимка настроек для списка интерфейсов.
     * \param settings Текущие настройки устройства.
     * \return Короткую строку вида `"115200 8-N-1"` либо пустую, чтобы колонку не
     *         показывать.
     *
     * Ядро не может составить её само: какие параметры существенны и как они читаются
     * вместе — знание транспорта. Для UART это скорость и режим, для TCP — адрес и порт.
     */
    virtual QString settingsSummary(const QVariantMap &settings) const
    {
        Q_UNUSED(settings);
        return {};
    }

    /**
     * \brief Пункты для поля схемы с SettingsField::live, найденные к этому моменту.
     * \param descriptor Устройство, чьи настройки сейчас показаны.
     * \param key Ключ поля схемы (SettingsField::key).
     * \param settings Текущие значения остальных полей — опрос почти всегда зависит от
     *        них (скорость шины нужно знать раньше, чем начать искать на ней узлы).
     * \return Найденное сейчас. Пустой список — ещё ничего не найдено, это не ошибка.
     *
     * \par Вызов — это и запрос, и продление
     *
     * Ядро зовёт метод, пока поле видно на экране, примерно раз в секунду, и перестаёт,
     * когда диалог закрыт. Другого сигнала «опрос больше не нужен» у плагина нет и не
     * требуется: прекратившиеся вызовы и есть этот сигнал. Опрос, следовательно, надо
     * заводить лениво — при первом вызове, — и гасить по молчанию, иначе шина останется
     * занятой после того, как пользователь закрыл окно.
     *
     * \warning Вызывается в потоке UI. Возвращать нужно накопленное, а не ждать ответа
     *          устройств внутри вызова.
     */
    virtual QList<SettingsOption> liveOptions(const InterfaceDescriptor &descriptor,
                                              const QString &key, const QVariantMap &settings)
    {
        Q_UNUSED(descriptor);
        Q_UNUSED(key);
        Q_UNUSED(settings);
        return {};
    }

    /**
     * \brief Создать канал для устройства.
     * \param descriptor Дескриптор, полученный ранее из enumerate().
     * \return Новый канал; владение переходит вызывающей стороне. `nullptr`, если плагин
     *         не обслуживает такой дескриптор.
     *
     * \note Ядро перенесёт канал в поток ввода-вывода до вызова open(). Ресурсы,
     *       привязанные к потоку (таймеры, дескрипторы), нужно создавать в open(), а не
     *       здесь.
     */
    virtual IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) = 0;

    /**
     * \brief Версия API, против которой собран плагин.
     *
     * Переопределять не нужно. spotty::PluginManager отказывается загружать плагин, у
     * которого значение не совпадает с #SPOTTY_API_VERSION, и объясняет причину
     * пользователю.
     */
    virtual int apiVersion() const { return SPOTTY_API_VERSION; }

    /**
     * \brief Объект, извещающий о появлении и пропаже устройств.
     * \return Объект, испускающий сигнал `void devicesChanged()`, либо `nullptr`.
     *
     * Позволяет заменить опрос раз в секунду настоящими уведомлениями системы (udev,
     * WM_DEVICECHANGE, IOKit). Возврат `nullptr` оставляет опрос — это нормальный,
     * рабочий вариант.
     *
     * \warning Возвращённый объект обязан жить не меньше самого плагина: реестр
     *          подключается к нему один раз при запуске.
     * \note Базового класса для уведомителя в SDK нет специально — реестр подключается к
     *       сигналу по имени, поэтому подойдёт любой QObject с нужной сигнатурой.
     */
    virtual QObject *hotplugNotifier() { return nullptr; }
};

} // namespace spotty

Q_DECLARE_INTERFACE(spotty::IInterfacePlugin, SPOTTY_INTERFACE_PLUGIN_IID)
