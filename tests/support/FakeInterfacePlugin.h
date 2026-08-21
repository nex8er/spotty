/**
 * \file FakeInterfacePlugin.h
 * \brief Поддельный плагин интерфейса для тестов ядра.
 */
#pragma once

#include <spotty/api/IInterfaceChannel.h>
#include <spotty/api/IInterfacePlugin.h>

#include <QAtomicInteger>

#include <utility>

namespace spotty::test {

/**
 * \class FakeChannel
 * \brief Канал, который ничего не делает, но послушно меняет состояние.
 */
class FakeChannel : public IInterfaceChannel
{
    Q_OBJECT

public:
    bool open(const QVariantMap &settings, QString *error) override
    {
        if (failToOpen) {
            if (error)
                *error = QStringLiteral("refused by test");
            return false;
        }
        lastSettings = settings;
        m_state = ChannelState::Open;
        Q_EMIT stateChanged(m_state, {});
        return true;
    }

    void close() override
    {
        m_state = ChannelState::Closed;
        Q_EMIT stateChanged(m_state, {});
    }

    qint64 write(const QByteArray &data) override
    {
        written.append(data);
        return data.size();
    }

    ChannelState state() const override { return m_state; }

    QVariantMap controlLines() const override
    {
        return {{QStringLiteral("CTS"), true}, {QStringLiteral("DSR"), false}};
    }

public Q_SLOTS:
    /**
     * \brief Изобразить приход данных от устройства.
     *
     * Слот, а не обычный метод: канал живёт в потоке ввода-вывода, и вызывать его из
     * теста напрямую нельзя — только через очередь событий его потока.
     */
    void injectData(const QByteArray &data)
    {
        Q_EMIT dataReceived(data, monotonicNs);
    }

    /// \brief Изобразить пропажу устройства изнутри канала.
    void injectUnavailable()
    {
        m_state = ChannelState::Unavailable;
        Q_EMIT stateChanged(m_state, QStringLiteral("gone"));
    }

public:
    /// \brief Настройки, с которыми канал открыли, — для проверки их нормализации.
    QVariantMap lastSettings;

    /// \brief Всё, что было записано в канал.
    QByteArray written;

    /// \brief Отметка времени, подставляемая в injectData().
    qint64 monotonicNs = 0;

    /// \brief Заставить open() завершиться неудачей.
    bool failToOpen = false;

private:
    ChannelState m_state = ChannelState::Closed;
};

/**
 * \class FakeInterfacePlugin
 * \brief Плагин с управляемым из теста списком устройств.
 *
 * \par Зачем он нужен
 *
 * Реестр интерфейсов невозможно проверить на настоящем плагине: появление и пропажу
 * устройства пришлось бы изображать физическим втыканием кабеля. Здесь список задаётся
 * присваиванием, и проверяются ровно те переходы, ради которых реестр написан.
 *
 * Заодно это вторая, после `loopback`, независимая реализация SDK — если API удобно
 * ложится и сюда, значит он не привязан к последовательному порту.
 */
class FakeInterfacePlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    /**
     * \param id Идентификатор плагина. Задаётся из теста там, где плагинов нужно
     *        несколько: отличить их друг от друга больше нечем.
     */
    explicit FakeInterfacePlugin(QString id = QStringLiteral("fake"))
        : m_id(std::move(id))
    {
    }

    QString pluginId() const override { return m_id; }
    QString displayName() const override { return QStringLiteral("Fake"); }

    QList<InterfaceDescriptor> enumerate() const override
    {
        ++enumerationCount;
        return devices;
    }

    SettingsSchema settingsSchema() const override
    {
        SettingsSchema schema;
        schema.add(SettingsField{
            .key = QStringLiteral("speed"),
            .label = QStringLiteral("Speed"),
            .group = QStringLiteral("Port"),
            .type = SettingsField::Integer,
            .defaultValue = 9600,
            .minimum = 1,
            .maximum = 1'000'000,
        });
        schema.add(SettingsField{
            .key = QStringLiteral("mode"),
            .label = QStringLiteral("Mode"),
            .group = QStringLiteral("Port"),
            .type = SettingsField::Text,
            .defaultValue = QStringLiteral("8N1"),
            .required = requireMode,
        });
        return schema;
    }

    QString settingsSummary(const QVariantMap &settings) const override
    {
        return QStringLiteral("%1 %2")
            .arg(settings.value(QStringLiteral("speed")).toInt())
            .arg(settings.value(QStringLiteral("mode")).toString());
    }

    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override
    {
        if (!descriptor.id.startsWith(QStringLiteral("fake:")))
            return nullptr;
        lastChannel = new FakeChannel;
        lastChannel->failToOpen = failNextOpen;
        return lastChannel;
    }

    /**
     * \brief Версия API, которую сообщит плагин.
     *
     * Подменяется тестом, чтобы проверить отказ по несовпадению версий: обычный плагин
     * apiVersion() не переопределяет вовсе.
     */
    int apiVersionOverride = SPOTTY_API_VERSION;

    int apiVersion() const override { return apiVersionOverride; }

    /// \brief Заставить следующий созданный канал отказать при открытии.
    bool failNextOpen = false;

    /// \brief Сделать поле "mode" обязательным (SettingsField::required) для теста Session.
    bool requireMode = false;

    /// \brief Список устройств, которые «видит» плагин. Меняется прямо из теста.
    QList<InterfaceDescriptor> devices;

    /**
     * \brief Сколько раз реестр опросил плагин.
     *
     * Атомарный, а не простой int: InterfaceRegistry теперь зовёт enumerate() из
     * фонового потока (InterfaceEnumerationWorker), а тест читает счётчик из своего —
     * иначе гонка между инкрементом и чтением.
     */
    mutable QAtomicInteger<int> enumerationCount = 0;

    /// \brief Последний созданный канал; принадлежит вызывающей стороне.
    FakeChannel *lastChannel = nullptr;

    /// \brief Собрать дескриптор с заданным идентификатором.
    static InterfaceDescriptor makeDevice(const QString &id, const QString &systemName,
                                          bool hiddenByDefault = false)
    {
        InterfaceDescriptor descriptor;
        descriptor.id = QStringLiteral("fake:") + id;
        descriptor.systemName = systemName;
        descriptor.description = QStringLiteral("Fake device");
        descriptor.hiddenByDefault = hiddenByDefault;
        return descriptor;
    }

private:
    QString m_id;
};

} // namespace spotty::test
