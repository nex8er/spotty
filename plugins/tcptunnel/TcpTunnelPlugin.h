/**
 * \file TcpTunnelPlugin.h
 * \brief Плагин проброса UART удалённой машины по raw TCP.
 */
#pragma once

#include "AgentPackage.h"
#include "HostNames.h"
#include "KnownHosts.h"
#include "NetworkScanner.h"
#include "RemoteTasks.h"

#include <spotty/api/IInterfacePlugin.h>

#include <QObject>
#include <QSet>

#include <memory>

namespace spotty {

/**
 * \class TcpTunnelDeviceNotifier
 * \brief Сообщает реестру, что подписи подключений могли измениться.
 *
 * Подпись составляется из имени хоста, а оно узнаётся уже после ввода адреса — от самой машины по
 * ssh или из обратного DNS сканера. Реестр на сигнал перечитывает устройства и пересчитывает
 * подписи (см. spotty::InterfaceRegistry).
 */
class TcpTunnelDeviceNotifier : public QObject
{
    Q_OBJECT

Q_SIGNALS:
    void devicesChanged();
};

/**
 * \class TcpTunnelPlugin
 * \brief UART на другой машине как обычный интерфейс Spotty.
 *
 * \par Откуда берутся устройства
 *
 * enumerate() ничего не возвращает: у сетевого подключения нет ни порта в системе, ни
 * серийного номера. Подключения заводит сам пользователь — см.
 * spotty::IInterfacePlugin::supportsUserDevices(), — а ядро хранит их вместе с настройками и
 * псевдонимом. Системное имя подключения — его адрес (`host:port`); подпись по умолчанию, она же
 * автоматический псевдоним, — `имя ttyUSB0` (см. tcptunnel::describeEndpoint()).
 *
 * \par Что стоит на другой стороне
 *
 * Агент `spotty-uartd` (agent/spotty-uartd.c): один процесс — один UART и один TCP-порт.
 * Несколько UART одной машины — несколько агентов на портах 7777, 7778…, то есть
 * несколько подключений с разным портом.
 *
 * \see spotty::TcpTunnelChannel
 */
class TcpTunnelPlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "tcptunnel.json")
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    TcpTunnelPlugin();
    ~TcpTunnelPlugin() override;

    /// \copydoc spotty::IInterfacePlugin::pluginId
    QString pluginId() const override { return QStringLiteral("tcptunnel"); }

    /// \copydoc spotty::IInterfacePlugin::displayName
    QString displayName() const override { return tr("TCP tunnel (remote UART)"); }

    /// \copydoc spotty::IInterfacePlugin::enumerate
    QList<InterfaceDescriptor> enumerate() const override { return {}; }

    /// \copydoc spotty::IInterfacePlugin::settingsSchema
    SettingsSchema settingsSchema() const override;

    /// \copydoc spotty::IInterfacePlugin::createChannel
    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override;

    /// \copydoc spotty::IInterfacePlugin::hotplugNotifier
    QObject *hotplugNotifier() override { return &m_notifier; }

    /**
     * \brief Имя и пароль ssh спрашиваются отдельным окном перед действием.
     *
     * Не поля настроек подключения: настройки лежат в файле, а то, что нужно одному нажатию кнопки,
     * хранить незачем. Пользователь, пароль и вход по ключу — в «Advanced» окна.
     */
    SettingsSchema promptSchema(const QString &actionKey, const QVariantMap &settings) const override;

    /**
     * \brief Найденное сканером: хосты для поля адреса, порты для поля порта, ход поиска.
     *
     * Сканирование идёт само, асинхронно, в потоке UI; здесь только отдаётся накопленное.
     */
    QList<SettingsOption> liveOptions(const InterfaceDescriptor &descriptor, const QString &key,
                                      const QVariantMap &settings) override;

    /// \brief Кнопка «Scan network»: начать (или начать заново) поиск агентов.
    QString triggerAction(const InterfaceDescriptor &descriptor, const QString &key,
                          const QVariantMap &settings) override;

    /// \copydoc spotty::IInterfacePlugin::supportsUserDevices
    bool supportsUserDevices() const override { return true; }

    /// \copydoc spotty::IInterfacePlugin::describeUserDevice
    InterfaceDescriptor describeUserDevice(const QVariantMap &settings) const override;

    /// \brief Выжимка настроек порта машины: `115200 8N1`.
    QString settingsSummary(const QVariantMap &settings) const override;

    /// \brief Каталог для отпечатков ssh-хостов, которым Spotty доверился.
    void setDataDirectory(const QString &path) override;

    /**
     * \brief Подменить ssh и комплект агента — только для тестов.
     * \param factory Создаёт сеанс; с ним установка считается доступной, даже если сборка без libssh2.
     * \param package Комплект, который заливается на машину.
     */
    void setSshForTesting(tcptunnel::RemoteTasks::SessionFactory factory,
                          std::shared_ptr<tcptunnel::AgentPackage> package);

    /// \brief Доступна ли установка по ssh в этой сборке (есть libssh2 или подставной сеанс).
    bool sshAvailable() const { return m_tasks != nullptr; }

    /// \brief Операции установки; нужны тестам, чтобы дождаться их конца.
    const tcptunnel::RemoteTasks *tasks() const { return m_tasks.get(); }

private:
    /**
     * \brief Строка хода или итога поиска; пустая, пока поиск не запускался.
     * \param severity Как её подать: итог поиска — успех или предупреждение, ход — нейтрально.
     */
    QString scanStatusText(NoteSeverity *severity) const;

    /// \brief Запомнить имя хоста, если оно ново, и сообщить реестру: подпись подключения изменится.
    void learnName(const QString &host, const QString &name);

    /// \brief Имена, которые сканер нашёл обратным DNS.
    void learnScannedNames();

    /// \brief Действия установки (поиск портов, установка, удаление, «забыть ключ»).
    QString triggerRemoteAction(const InterfaceDescriptor &descriptor, const QString &key,
                                const QVariantMap &settings);

    /// \brief Поля схемы, связанные с ssh; добавляются, только если установка доступна.
    void addInstallerFields(SettingsSchema &schema) const;

    /// \brief Что просканировано в последний раз — для строки итога: «192.168.1.0/24».
    QString m_scanScope;

    std::shared_ptr<tcptunnel::KnownHosts> m_knownHosts = std::make_shared<tcptunnel::KnownHosts>();
    std::shared_ptr<tcptunnel::HostNames> m_hostNames = std::make_shared<tcptunnel::HostNames>();
    TcpTunnelDeviceNotifier m_notifier;

    /// \brief Адреса, для которых уже спрашивали обратный DNS: не чаще одного запроса за сеанс.
    QSet<QString> m_nameLookups;
    std::unique_ptr<tcptunnel::RemoteTasks> m_tasks;

    /// \brief Один сканер на плагин: найденное нужно любому диалогу настроек, не только открывшему поиск.
    NetworkScanner m_scanner;
};

} // namespace spotty
