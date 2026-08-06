/**
 * \file SettingsStore.h
 * \brief Хранилище настроек в формате JSON.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

class QTimer;

namespace spotty {

/**
 * \class SettingsStore
 * \brief Хранилище «ключ — значение» поверх файла JSON.
 *
 * \par Ключи
 *
 * Ключ — путь, разделённый косой чертой, который отображается на вложенные объекты JSON.
 * Запись `value("terminal/fontSize")` читает:
 *
 * \code{.json}
 * { "terminal": { "fontSize": 12 } }
 * \endcode
 *
 * \par Почему JSON, а не QSettings
 *
 * Файл остаётся читаемым и правится руками, переносится между операционными системами и
 * кладётся в систему контроля версий. QSettings пишет в реестр Windows, plist на macOS и
 * ini на Linux — перенести настройки между машинами невозможно, а вложенные структуры
 * (пресеты макросов, настройки устройств) хранить неудобно.
 *
 * \par Запись
 *
 * setValue() лишь помечает хранилище изменённым и запускает отложенную запись, поэтому
 * перетаскивание ползунка не приводит к сотне обращений к диску. Там, где потеря
 * недопустима — закрытие окна, применение диалога, — нужно вызывать save() явно.
 * Деструктор дописывает несохранённое.
 */
class SettingsStore : public QObject
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param filePath Путь к файлу JSON. Файла может не быть — это нормально при первом
     *        запуске.
     * \param parent Родитель в дереве QObject.
     *
     * \note Конструктор ничего не читает; нужно вызвать load().
     */
    explicit SettingsStore(QString filePath, QObject *parent = nullptr);

    /// \brief Деструктор. Сбрасывает на диск несохранённые изменения.
    ~SettingsStore() override;

    /// \return Путь к файлу, с которым работает хранилище.
    QString filePath() const { return m_filePath; }

    /**
     * \brief Прочитать значение.
     * \param key Ключ, разделённый косой чертой.
     * \param fallback Что вернуть, если ключа нет.
     */
    QVariant value(const QString &key, const QVariant &fallback = {}) const;

    /**
     * \brief Записать значение.
     * \param key Ключ, разделённый косой чертой. Промежуточные объекты создаются сами.
     * \param value Новое значение.
     *
     * Если значение не изменилось, ничего не происходит — ни сигнала, ни записи на диск.
     * Это важно: список интерфейсов перестраивается раз в секунду и без такой проверки
     * дёргал бы файл вхолостую.
     */
    void setValue(const QString &key, const QVariant &value);

    /// \return `true`, если ключ существует.
    bool contains(const QString &key) const;

    /// \brief Удалить ключ.
    void remove(const QString &key);

    /**
     * \brief Удалить всё содержимое.
     *
     * Для сброса приложения к умолчаниям: и `settings.json`, и `interfaces.json` — один и
     * тот же класс хранилища, отдельного метода на каждый файл заводить незачем.
     */
    void clear();

    /**
     * \brief Прочитать целое поддерево как карту.
     * \param key Ключ поддерева.
     *
     * Используется для настроек устройства и пресетов макросов, где значение —
     * не скаляр, а вложенный объект.
     */
    QVariantMap group(const QString &key) const;

    /// \brief Заменить целое поддерево.
    void setGroup(const QString &key, const QVariantMap &values);

    /// \return Все данные целиком. Нужен реестру интерфейсов для обхода всех записей.
    const QVariantMap &data() const { return m_data; }

    /**
     * \brief Прочитать файл.
     * \return `true` при успехе или если файла ещё нет.
     *
     * \note Испорченный JSON — не повод перезаписать файл. Он остаётся нетронутым, а
     *       программа продолжает с умолчаниями: это единственный экземпляр настроек
     *       пользователя, и он может захотеть починить его руками.
     */
    bool load();

    /**
     * \brief Немедленно записать файл.
     * \return `true` при успехе.
     *
     * Запись идёт через QSaveFile, то есть во временный файл с последующим
     * переименованием. Прерванная запись — из-за нехватки места или снятия процесса — не
     * может оставить обрезанный конфиг.
     */
    bool save();

Q_SIGNALS:
    /**
     * \brief Значение изменилось.
     * \param key Изменившийся ключ.
     * \param value Новое значение; при удалении — недействительный QVariant.
     */
    void valueChanged(const QString &key, const QVariant &value);

private:
    /// \brief Пометить изменённым и перезапустить таймер отложенной записи.
    void scheduleSave();

    QString m_filePath;      ///< Путь к файлу.
    QVariantMap m_data;      ///< Данные в памяти.
    QTimer *m_saveTimer = nullptr; ///< Таймер отложенной записи.
    bool m_dirty = false;    ///< Есть ли несохранённые изменения.
};

} // namespace spotty
