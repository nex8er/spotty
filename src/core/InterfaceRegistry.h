/**
 * \file InterfaceRegistry.h
 * \brief Реестр доступных интерфейсов.
 */
#pragma once

#include <spotty/api/InterfaceDescriptor.h>

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class QTimer;

namespace spotty {

class PluginManager;
class SettingsStore;

/**
 * \struct InterfaceEntry
 * \brief Устройство глазами ядра: дескриптор плюс всё, что решил пользователь.
 *
 * \see spotty::InterfaceDescriptor — та же сущность глазами плагина.
 */
struct InterfaceEntry
{
    InterfaceDescriptor descriptor; ///< Что сообщил плагин.

    /// \brief Имя, заданное пользователем. Пустая строка — показывать системное имя.
    QString alias;

    /// \brief Присутствует ли устройство в системе прямо сейчас.
    bool present = false;

    /**
     * \brief Когда текущий запуск Spotty впервые увидел это устройство.
     *
     * \note Намеренно не сохраняется между запусками. Система не сообщает, когда было
     *       подключено устройство, уже присутствовавшее на момент старта программы, а
     *       выдумывать значение из прошлой сессии хуже, чем честное «видно с момента
     *       запуска Spotty». Отсюда же сброс поля при пропаже устройства: следующее
     *       появление начинает отсчёт заново.
     */
    QDateTime discoveredAt;

    /// \brief Настройки, приведённые к схеме плагина-владельца.
    QVariantMap settings;

    /// \return Псевдоним, если задан, иначе системное имя.
    QString displayName() const
    {
        return alias.isEmpty() ? descriptor.systemName : alias;
    }
};

/**
 * \class InterfaceRegistry
 * \brief Единственный источник истины о том, к чему можно подключиться.
 *
 * Сводит воедино результаты enumerate() всех плагинов, помнит псевдонимы и настройки в
 * `interfaces.json` и отслеживает появление и пропажу устройств, чтобы сессия могла
 * закрыть потерянный порт и открыть его снова при возвращении.
 *
 * \par Обнаружение изменений
 *
 * Если плагин предоставил hotplugNotifier(), реестр подписывается на его сигнал
 * `devicesChanged()`. Плагины без уведомителя обслуживаются опросом раз в секунду.
 * Оба механизма сводятся к одному методу refresh(), поэтому остальной программе всё
 * равно, каким способом узнали об изменении.
 *
 * \par Что сохраняется
 *
 * В `interfaces.json` под ключом InterfaceDescriptor::id: идентификатор плагина,
 * системное имя, описание, псевдоним и настройки. Признак присутствия и время
 * обнаружения — состояние времени выполнения и не сохраняются.
 */
class InterfaceRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param plugins Менеджер плагинов; должен пережить реестр.
     * \param store Хранилище, привязанное к `interfaces.json`.
     * \param parent Родитель в дереве QObject.
     */
    InterfaceRegistry(PluginManager *plugins, SettingsStore *store, QObject *parent = nullptr);
    ~InterfaceRegistry() override;

    /**
     * \brief Прочитать сохранённое, выполнить первый обход и начать следить за изменениями.
     *
     * \note Вызывать после PluginManager::load(): подписка на уведомители устройств
     *       делается один раз, по уже загруженным плагинам.
     */
    void start();

    /**
     * \brief Все известные устройства.
     * \return Сначала присутствующие, затем запомненные, но отсутствующие; внутри каждой
     *         группы — по отображаемому имени.
     *
     * Отсутствующие не выбрасываются из списка намеренно: настроенный порт, который сейчас
     * не подключён, должен оставаться видимым, а не исчезать без следа.
     */
    QList<InterfaceEntry> entries() const;

    /**
     * \brief Найти запись по идентификатору.
     * \return Указатель или `nullptr`.
     * \warning Указатель действителен до следующего refresh().
     */
    const InterfaceEntry *entry(const QString &id) const;

    /// \brief Задать псевдоним устройства. Сохраняется немедленно.
    void setAlias(const QString &id, const QString &alias);

    /**
     * \brief Настройки устройства, приведённые к текущей схеме плагина-владельца.
     *
     * Нормализация именно при чтении — то, что позволяет конфигурации, записанной старой
     * версией плагина, пережить появление или исчезновение параметра.
     *
     * \see spotty::SettingsSchema::normalized()
     */
    QVariantMap settingsFor(const QString &id) const;

    /// \brief Записать настройки устройства. Сохраняется немедленно.
    void setSettingsFor(const QString &id, const QVariantMap &settings);

public Q_SLOTS:
    /**
     * \brief Опросить все плагины и сообщить об изменениях.
     *
     * Вызывается по таймеру опроса, по сигналу уведомителя устройств и вручную.
     */
    void refresh();

Q_SIGNALS:
    /// \brief Список устройств изменился — потребителю достаточно перестроить отображение.
    void changed();

    /**
     * \brief Устройство стало доступно.
     * \param id Идентификатор устройства.
     *
     * Сессия использует сигнал, чтобы заново открыть порт, потерянный ранее.
     */
    void interfaceAppeared(const QString &id);

    /**
     * \brief Устройство пропало.
     * \param id Идентификатор устройства.
     *
     * Владеющая сессия обязана закрыть канал и перейти в ChannelState::Unavailable.
     */
    void interfaceDisappeared(const QString &id);

private:
    /// \brief Загрузить запомненные устройства из `interfaces.json`.
    void loadPersisted();

    /// \brief Записать одну запись в хранилище.
    void persist(const QString &id) const;

    PluginManager *m_plugins;       ///< Источник плагинов.
    SettingsStore *m_store;         ///< Хранилище `interfaces.json`.
    QTimer *m_pollTimer = nullptr;  ///< Таймер опроса; nullptr, если все плагины с уведомителями.
    QHash<QString, InterfaceEntry> m_entries; ///< Известные устройства по идентификатору.
};

} // namespace spotty
