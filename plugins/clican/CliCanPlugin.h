/**
 * \file CliCanPlugin.h
 * \brief Плагин командной строки платы через туннель CAN.
 */
#pragma once

#include "CanBus.h"

#include <spotty/api/IInterfacePlugin.h>

#include <QElapsedTimer>
#include <QObject>

#include <memory>
#include <thread>

class QTimer;

namespace spotty {

/**
 * \class CliCanPlugin
 * \brief Доступ к CLI плат, подключённых к шине CAN, через адаптер PCAN.
 *
 * Устройство здесь — адаптер, а не плата: скорость шины нужно знать раньше, чем на ней
 * удастся кого-то найти. Плата выбирается настройкой «Node», список которой заполняется
 * опросом (SettingsField::live) — диалог открывается сразу, а узлы появляются в нём по
 * мере ответов.
 *
 * \see spotty::CliCanChannel
 * \see docs/CLICAN.md
 */
class CliCanPlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "clican.json")
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    CliCanPlugin();
    ~CliCanPlugin() override;

    /// \copydoc spotty::IInterfacePlugin::pluginId
    QString pluginId() const override { return QStringLiteral("clican"); }

    /// \copydoc spotty::IInterfacePlugin::displayName
    QString displayName() const override { return tr("CLI over CAN (PCAN)"); }

    /// \copydoc spotty::IInterfacePlugin::enumerate
    QList<InterfaceDescriptor> enumerate() const override;

    /// \copydoc spotty::IInterfacePlugin::settingsSchema
    SettingsSchema settingsSchema() const override;

    /// \brief Выжимка вида `"node 5, 500 kbit/s"`.
    QString settingsSummary(const QVariantMap &settings) const override;

    /// \copydoc spotty::IInterfacePlugin::liveOptions
    QList<SettingsOption> liveOptions(const InterfaceDescriptor &descriptor, const QString &key,
                                      const QVariantMap &settings) override;

    /// \copydoc spotty::IInterfacePlugin::createChannel
    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override;

private:
    /// \brief Отпустить шину, если диалог настроек давно не спрашивал про узлы.
    void expireScan();

    /**
     * \brief Начать открытие шины поиска в фоновом потоке.
     *
     * liveOptions() зовётся из потока UI и не вправе блокировать его, а
     * `CanBusPool::acquire()` внутри делает `PCAN_Initialize()` — блокирующий вызов
     * драйвера, занимающий заметное время. Открытие уходит в отдельный поток; результат
     * возвращается в поток UI через `QMetaObject::invokeMethod()`, как и у CliCanChannel.
     */
    void beginScanOpen(PcanLibrary::Handle handle, int bitrate);

    /// \brief Принять результат фонового открытия; зовётся уже в потоке UI.
    void finishScanOpen(PcanLibrary::Handle handle, int bitrate, std::shared_ptr<CanBus> bus);

    /**
     * \brief Общий пул шин — этого плагина и только его.
     *
     * Не синглтон намеренно (см. \par «Почему не синглтон» в CanBus.h): объявлен раньше
     * #m_scanBus, поэтому при разрушении CliCanPlugin поля уничтожаются в обратном
     * порядке — #m_scanBus (и его удалитель, обращающийся к пулу) раньше, чем сам пул.
     * createChannel() отдаёт указатель на него же каждому CliCanChannel.
     */
    CanBusPool m_pool;

    /**
     * \brief Шина, открытая ради поиска узлов.
     *
     * Живёт только пока открыт диалог настроек: единственный признак его закрытия —
     * прекратившиеся вызовы liveOptions(), поэтому шина держится по таймеру и отпускается
     * по молчанию (см. #kScanIdleMs).
     */
    std::shared_ptr<CanBus> m_scanBus;
    PcanLibrary::Handle m_scanHandle = 0;
    int m_scanBitrate = 0;
    QElapsedTimer m_scanRequested;
    QTimer *m_scanTimer = nullptr;

    /**
     * \brief Открытие в фоновом потоке ещё не завершилось.
     *
     * Пока `true`, liveOptions() не заводит новую попытку, даже если пользователь успел
     * поменять скорость или устройство, — пришедший результат применяется, а несовпадение
     * с желаемым тут же поднимает следующую, уже верную попытку на очередном тике. Второй
     * параллельный поток был бы лишней сложностью ради случая, которого пользователь не
     * заметит: цикл «открыли не то — тут же закрыли — открыли верное» укладывается в
     * секунду между двумя вызовами.
     */
    bool m_scanOpening = false;
    std::thread m_scanOpenThread;

    /// \brief Сколько шина ищет узлы после последнего вопроса о них.
    static constexpr int kScanIdleMs = 3000;
};

} // namespace spotty
