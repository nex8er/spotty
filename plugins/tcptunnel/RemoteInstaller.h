/**
 * \file RemoteInstaller.h
 * \brief Установка, перенастройка и удаление агента на удалённой машине по ssh.
 */
#pragma once

#include "AgentPackage.h"
#include "KnownHosts.h"
#include "RemoteHost.h"
#include "SshSession.h"

#include <QCoreApplication>
#include <QList>
#include <QString>

#include <functional>

namespace spotty::tcptunnel {

/**
 * \class RemoteInstaller
 * \brief Последовательность шагов поверх ISshSession; сам ни сети, ни libssh2 не знает.
 *
 * \par Принципы
 *
 * - **Пользовательские строки не попадают в shell.** Скриптам уходят только числа; путь порта,
 *   список адресов и прочее лежит в конфиге, залитом файлом (`cat > файл` со входным
 *   потоком), а в командной строке — лишь пути каталога, созданного нами самими, в кавычках
 *   (shellQuote()).
 * - **Ничего не остаётся на машине при сбое.** Промежуточный каталог убирается на любом
 *   выходе.
 * - **Ключ хоста проверяется до пароля.** См. KnownHosts.
 *
 * Блокирующий: зовётся из рабочего потока. Объект на один сеанс.
 */
class RemoteInstaller
{
    // Не QObject, но строки интерфейса: tr() с контекстом «RemoteInstaller», который знает lupdate.
    Q_DECLARE_TR_FUNCTIONS(RemoteInstaller)

public:
    /// \brief Связь с внешним миром: ход работы и просьба прекратить.
    struct Hooks
    {
        std::function<void(const QString &)> progress; ///< Что делается сейчас.
        std::function<bool()> cancelled;               ///< `true` — прервать между шагами.
    };

    RemoteInstaller(ISshSession &session, const AgentPackage &package, KnownHosts &knownHosts,
                    Hooks hooks = {});

    /// \brief Подключиться, проверить ключ хоста и войти.
    bool connect(const SshAccess &access, QString *error);

    /**
     * \brief Отпечаток неизвестного ключа, из-за которого connect() отказал; пусто, если отказ другой.
     *
     * Вызывающий запоминает его и при повторе действия передаёт как SshAccess::trustFingerprint:
     * так «подтвердить» значит повторить после того, как отпечаток показан.
     */
    const QString &refusedFingerprint() const { return m_refusedFingerprint; }

    /// \brief Узнать, что за машина, и подобрать под неё агент.
    bool probe(HostInfo *info, QString *error);

    /// \brief Список последовательных портов машины (запускает залитый агент с `--scan`).
    bool scanUarts(const HostInfo &info, QList<UartPort> *ports, QString *error);

    /// \brief Поставить или обновить экземпляр агента.
    bool install(const HostInfo &info, const InstallParams &params, QString *error);

    /// \brief Убрать экземпляр агента на порту; последний уносит и сам бинарник.
    bool uninstall(const HostInfo &info, int port, QString *error);

private:
    void progress(const QString &text) const;
    bool cancelled() const;

    bool run(const QString &command, const QByteArray &input, int timeoutMs, SshExecResult *result,
             QString *error);

    /// \brief Выполнить команду от root: напрямую, если мы root, иначе через sudo с паролем.
    bool runPrivileged(const HostInfo &info, const QString &command, int timeoutMs,
                       SshExecResult *result, QString *error);

    /// \brief Создать каталог для файлов установки. `base` — где: `/tmp` или домашний.
    bool makeStage(const QString &base, QString *dir, QString *error);

    /// \brief Залить файл: `cat > путь && chmod режим путь`.
    bool put(const QString &path, const QByteArray &data, int mode, QString *error);

    void removeStage(const QString &dir);

    /// \brief Каталог с запускаемым агентом; пробует `/tmp`, затем домашний (`/tmp` бывает noexec).
    bool prepareAgentStage(const HostInfo &info, QString *dir, QString *error);

    /// \brief Запустить залитого в `stage` агента с `--scan` и разобрать список портов.
    bool listPorts(const QString &stage, QList<UartPort> *ports, QString *error);

    ISshSession &m_session;
    const AgentPackage &m_package;
    KnownHosts &m_knownHosts;
    Hooks m_hooks;
    SshAccess m_access;
    QString m_refusedFingerprint;
};

/// \brief Достать из вывода скрипта последнюю строку `ERROR: ...`; пусто, если её нет.
QString lastScriptError(const QString &scriptOutput);

} // namespace spotty::tcptunnel
