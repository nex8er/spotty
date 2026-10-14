/**
 * \file RemoteTasks.h
 * \brief Фоновые операции с удалённой машиной: поиск портов, установка, удаление.
 */
#pragma once

#include "AgentPackage.h"
#include "KnownHosts.h"
#include "RemoteHost.h"
#include "SshSession.h"

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>

#include <functional>
#include <memory>

namespace spotty::tcptunnel {

/**
 * \class RemoteTasks
 * \brief Запускает операции установщика в рабочих потоках и хранит их ход и итог для каждого подключения.
 *
 * \par Зачем отдельный класс
 *
 * Плагин интерфейса не вправе блокировать поток UI, а ssh-операции идут секундами: подключение,
 * определение машины, заливка, установка. Здесь они уходят в поток, а диалог настроек видит только
 * снимок состояния (snapshot()), который плагин отдаёт через живые поля.
 *
 * \par Время жизни
 *
 * Поток не ссылается на сам RemoteTasks: всё, что ему нужно (хранилище ключей, комплект, фабрика
 * сеансов, состояние), он держит через shared_ptr. Деструктор поэтому только просит прекратить и
 * не ждёт: при выходе из программы плагин разрушается во время финализации статиков, и ждать там
 * ssh-операцию, у которой срок до полутора минут, значило бы повесить закрытие.
 */
class RemoteTasks
{
    Q_DECLARE_TR_FUNCTIONS(RemoteTasks)

public:
    using SessionFactory = std::function<std::unique_ptr<ISshSession>()>;

    enum class Kind {
        FindPorts, ///< Подключиться и выяснить, какие последовательные порты есть на машине.
        Install,   ///< Поставить или обновить агента.
        Uninstall, ///< Удалить экземпляр агента.
    };

    /// \brief Состояние операции подключения для показа в диалоге.
    struct Snapshot
    {
        bool known = false;     ///< Операций для этого подключения ещё не было.
        bool running = false;
        bool ok = false;        ///< Итог последней законченной операции.
        Kind kind = Kind::FindPorts;
        QString text;           ///< Ход (пока идёт) или итог.
        bool hasHost = false;
        HostInfo host;          ///< Что узнали о машине при последней операции.
        QList<UartPort> ports;  ///< Найденные последовательные порты.
    };

    RemoteTasks(std::shared_ptr<KnownHosts> knownHosts, std::shared_ptr<AgentPackage> package,
                SessionFactory factory);
    ~RemoteTasks();

    RemoteTasks(const RemoteTasks &) = delete;
    RemoteTasks &operator=(const RemoteTasks &) = delete;

    /**
     * \brief Начать операцию.
     * \param deviceId Подключение, которому она принадлежит.
     * \param params Для Install — все параметры; для Uninstall — важен только `port`.
     * \return Пустую строку, если запущено; иначе причину (например, предыдущая операция ещё идёт).
     */
    QString start(const QString &deviceId, Kind kind, const SshAccess &access, const InstallParams &params);

    Snapshot snapshot(const QString &deviceId) const;

    /// \brief Дождаться окончания всех операций. Для тестов: боевой код не ждёт.
    bool waitForIdle(int timeoutMs) const;

private:
    struct State;
    mutable QMutex m_mutex;
    QHash<QString, std::shared_ptr<State>> m_states;
    std::shared_ptr<KnownHosts> m_knownHosts;
    std::shared_ptr<AgentPackage> m_package;
    SessionFactory m_factory;
};

} // namespace spotty::tcptunnel
