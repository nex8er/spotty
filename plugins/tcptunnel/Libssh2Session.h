/**
 * \file Libssh2Session.h
 * \brief Настоящий ssh-сеанс на libssh2.
 */
#pragma once

#include "SshSession.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QString>

#include <cstdint>

struct _LIBSSH2_SESSION;
typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;

namespace spotty::tcptunnel {

/**
 * \class Libssh2Session
 * \brief ISshSession поверх libssh2.
 *
 * \par Почему собственный сокет, а не QTcpSocket
 *
 * libssh2 читает и пишет в дескриптор сама, а QTcpSocket при ожидании соединения может прочесть
 * из него первые байты — приветствие сервера, которое приходит сразу, — и положить к себе в
 * буфер. Обмен ключами тогда зависает, и причина совершенно не видна. Поэтому TCP-соединение
 * здесь создаётся напрямую (getaddrinfo, неблокирующий connect), а Qt в этой части не участвует.
 *
 * \par Блокирование
 *
 * Снаружи вызовы блокирующие, внутри libssh2 работает в неблокирующем режиме, и каждый вызов
 * обёрнут циклом ожидания по сокету с общим сроком. Так любую операцию можно ограничить по
 * времени, чего блокирующий режим libssh2 не даёт.
 *
 * Не потокобезопасен: один сеанс — один поток.
 */
class Libssh2Session : public ISshSession
{
    Q_DECLARE_TR_FUNCTIONS(Libssh2Session)

public:
    Libssh2Session();
    ~Libssh2Session() override;

    Libssh2Session(const Libssh2Session &) = delete;
    Libssh2Session &operator=(const Libssh2Session &) = delete;

    bool connectToHost(const QString &host, int port, int timeoutMs, QString *error) override;
    QString hostKeyFingerprint() const override { return m_fingerprint; }
    SystemHostKey checkSystemKnownHosts(const QString &host, int port) override;
    bool authenticate(const SshAccess &access, QString *error) override;
    bool exec(const QString &command, const QByteArray &input, int timeoutMs, SshExecResult *result,
              QString *error) override;
    void disconnectFromHost() override;

    /// \brief Пароль для обратного вызова keyboard-interactive (см. authenticate()).
    const QByteArray &passwordForCallback() const { return m_password; }

private:
    /// \brief Подождать готовности сокета в том направлении, которого ждёт libssh2. \return `false` — вышел срок.
    bool waitSocket(const QDeadlineTimer &deadline);

    /// \brief Повторять вызов libssh2, пока он отвечает EAGAIN. \return Код возврата или таймаут.
    template <typename Call>
    int drive(Call &&call, const QDeadlineTimer &deadline);

    /// \brief То же для вызовов, возвращающих указатель: NULL при EAGAIN — просьба подождать, а не отказ.
    template <typename Call>
    auto drivePtr(Call &&call, const QDeadlineTimer &deadline, bool *timedOut);

    /// \brief Последнее сообщение libssh2 как строка.
    QString lastError() const;

    void closeSocket();

    LIBSSH2_SESSION *m_session = nullptr;
    std::intptr_t m_socket = -1;
    QString m_fingerprint;
    QByteArray m_password;
};

} // namespace spotty::tcptunnel
