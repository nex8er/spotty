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
     * \brief Пользователь нажал кнопку поля схемы с типом SettingsField::Action.
     * \param descriptor Устройство, чьи настройки сейчас показаны.
     * \param key Ключ поля (SettingsField::key).
     * \param settings Текущие значения остальных полей.
     * \return Пустую строку при успехе, иначе текст, который ядро покажет пользователю.
     *         Допускается разметка Qt (rich text) со ссылками.
     *
     * Плагину интерфейса виджеты запрещены, поэтому кнопку строит ядро, а сюда приходит
     * только само действие. После вызова ядро сразу переспрашивает liveOptions(): то, что
     * действие изменило, становится видно без ожидания следующего тика.
     *
     * \warning Вызывается в потоке UI. Долгую работу нужно отдавать в фон, а здесь только
     *          запускать её.
     */
    virtual QString triggerAction(const InterfaceDescriptor &descriptor, const QString &key,
                                  const QVariantMap &settings)
    {
        Q_UNUSED(descriptor);
        Q_UNUSED(key);
        Q_UNUSED(settings);
        return {};
    }

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

    /**
     * \brief Заводит ли устройства сам пользователь.
     * \return `true` для транспорта, у которого нечего перечислять: адрес сетевого узла
     *         известен только тому, кто его ввёл.
     *
     * Ядро в этом случае показывает кнопку «новое подключение», хранит созданные
     * устройства само (в `interfaces.json`, вместе с псевдонимом и настройками) и **не
     * убирает их из списка, когда enumerate() их не вернул**. Эти устройства плагин
     * enumerate() не перечисляет вовсе — обычно он возвращает пустой список.
     *
     * Для остальных транспортов остаётся прежнее правило: устройство есть, пока его видит
     * система, а запись о нём живёт ровно столько же.
     *
     * \see describeUserDevice()
     */
    virtual bool supportsUserDevices() const { return false; }

    /**
     * \brief Описание устройства, созданного пользователем, по его настройкам.
     * \param settings Настройки устройства, приведённые к схеме.
     * \return Дескриптор с заполненными InterfaceDescriptor::systemName и описанием (поля
     *         InterfaceDescriptor::id и pluginId заполняет ядро); `systemName` — адрес
     *         вроде `192.168.1.50:7777`, `description` — подпись для списка.
     *
     * Ядро зовёт метод при создании устройства и после каждой правки настроек, поэтому
     * подпись в списке всегда соответствует тому, что в них введено — и это же служит
     * автоматическим псевдонимом: пока пользователь не задал свой, показывается она.
     *
     * \warning Вызывается в потоке UI и из фонового потока перечисления не вызывается;
     *          должен быть дешёвым и ничего не опрашивать.
     */
    virtual InterfaceDescriptor describeUserDevice(const QVariantMap &settings) const
    {
        Q_UNUSED(settings);
        return {};
    }

    /**
     * \brief Каталог, где плагин вправе хранить собственные данные.
     * \param path Путь, свой у каждого плагина; сам каталог может ещё не существовать.
     *
     * Ядро зовёт метод один раз, сразу после того, как плагин принят, — до первого
     * enumerate(). Каталог лежит рядом с остальной конфигурацией и переезжает вместе с ней
     * в переносном режиме, поэтому плагину не нужно знать ни `QStandardPaths`, ни маркер
     * переносного режима. Создавать его — дело плагина, и только когда ему есть что
     * записать: пустой каталог на каждый установленный плагин ни к чему.
     *
     * Реализация по умолчанию ничего не делает: большинству транспортов хранить нечего, а
     * их настройки уже лежат в `interfaces.json`.
     */
    virtual void setDataDirectory(const QString &path) { Q_UNUSED(path); }

    /**
     * \brief Что спросить у пользователя, прежде чем выполнить действие.
     * \param actionKey Ключ поля SettingsField::Action, которое нажали.
     * \param settings Текущие значения полей устройства.
     * \return Схему полей для окна вопроса; пустая — действие выполняется сразу.
     *
     * Для сведений, которые нужны одному действию и которые ни в коем случае не должны оседать в
     * настройках: имя и пароль для входа по ssh. Ядро показывает модальное окно по этой схеме
     * (поля #SettingsField::advanced свёрнуты, SettingsField::Password скрыты), а ввод передаёт
     * в triggerAction() добавленным к \p settings. Нигде, кроме памяти окна настроек, он не хранится:
     * после его закрытия вопрос задаётся заново.
     *
     * \note Вызывается в потоке UI перед каждым нажатием, поэтому должен быть дешёвым.
     */
    virtual SettingsSchema promptSchema(const QString &actionKey, const QVariantMap &settings) const
    {
        Q_UNUSED(actionKey);
        Q_UNUSED(settings);
        return {};
    }
};

} // namespace spotty

Q_DECLARE_INTERFACE(spotty::IInterfacePlugin, SPOTTY_INTERFACE_PLUGIN_IID)
