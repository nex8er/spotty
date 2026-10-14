/**
 * \file SshSession.h
 * \brief Сеанс ssh в объёме, нужном установщику: подключиться, проверить хост, войти, выполнить.
 */
#pragma once

#include <QByteArray>
#include <QString>

namespace spotty::tcptunnel {

/// \brief Куда и как входить.
struct SshAccess
{
    QString host;
    int port = 22;
    QString user;
    QString password;       ///< Для входа и для sudo. Не хранится нигде, кроме памяти.
    QString keyFile;        ///< Закрытый ключ; если задан, вход идёт по ключу.
    QString keyPassphrase;

    /**
     * \brief Отпечаток ключа, который пользователь уже видел и подтвердил.
     *
     * Неизвестный хост принимается, только если предъявил именно этот ключ: пользователь прочёл
     * отпечаток в сообщении об отказе и повторил действие. Слепого «доверять любому» нет —
     * подменённый ключ при повторе дал бы другой отпечаток и был бы снова отвергнут.
     */
    QString trustFingerprint;
    int timeoutMs = 15000;
};

/// \brief Итог выполнения одной команды.
struct SshExecResult
{
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
};

/// \brief Что известно о ключе хоста из файла `known_hosts` самой системы.
enum class SystemHostKey {
    NotFound, ///< Хоста там нет (или файла нет, или формат не прочитан).
    Match,    ///< Есть, ключ тот.
    Mismatch, ///< Есть, ключ другой.
};

/**
 * \class ISshSession
 * \brief Один сеанс ssh. Блокирующий: зовётся из рабочего потока, не из потока UI.
 *
 * Интерфейс узок намеренно. Установщик (RemoteInstaller) не знает про libssh2, а проверяется на
 * подставных сеансах; настоящий (Libssh2Session) проверяется отдельно, на настоящем sshd. Заливка
 * файлов — не метод сеанса, а команда `cat > файл` со входным потоком: так не нужны ни SFTP,
 * ни scp, которых нет на урезанных системах (dropbear, образы без openssh-client).
 */
class ISshSession
{
public:
    virtual ~ISshSession() = default;

    /// \brief Установить TCP-соединение и провести обмен ключами (без входа).
    virtual bool connectToHost(const QString &host, int port, int timeoutMs, QString *error) = 0;

    /// \brief Отпечаток ключа хоста вида `SHA256:...` — после connectToHost().
    virtual QString hostKeyFingerprint() const = 0;

    /// \brief Что говорит о ключе хоста `~/.ssh/known_hosts` пользователя — после connectToHost().
    virtual SystemHostKey checkSystemKnownHosts(const QString &host, int port) = 0;

    /// \brief Войти. Способ выбирается по полям: ключ, иначе пароль, иначе ssh-agent.
    virtual bool authenticate(const SshAccess &access, QString *error) = 0;

    /**
     * \brief Выполнить команду и дождаться конца.
     * \param command Строка для shell удалённой машины.
     * \param input Что подать на стандартный ввод (может быть пустым).
     * \param timeoutMs Сколько ждать завершения.
     * \param result Код возврата и оба потока вывода.
     * \param error Причина, если команду не удалось даже запустить или вышло время.
     * \return `false` — сам обмен не удался; ненулевой код возврата командой остаётся `true`.
     */
    virtual bool exec(const QString &command, const QByteArray &input, int timeoutMs,
                      SshExecResult *result, QString *error) = 0;

    virtual void disconnectFromHost() = 0;
};

} // namespace spotty::tcptunnel
