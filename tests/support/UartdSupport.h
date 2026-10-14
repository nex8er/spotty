/**
 * \file UartdSupport.h
 * \brief Средства для тестов, запускающих настоящий агент spotty-uartd.
 *
 * Агент запускается отдельным процессом, а последовательный порт ему подменяет
 * псевдотерминал: проверяется ровно то, что уйдёт в бинарники под все архитектуры.
 */
#pragma once

#include "support/TestSupport.h"

#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>

#include <gtest/gtest.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <memory>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace spotty::test {

/// \brief Все байты 0..255 подряд: поток не должен ни терять нули, ни переводить строки.
inline QByteArray everyByte()
{
    QByteArray data;
    for (int i = 0; i < 256; ++i)
        data.append(char(i));
    return data;
}

/**
 * \class FakeUart
 * \brief Псевдотерминал в роли последовательного порта.
 *
 * Ведущий конец принадлежит тесту: записанное в него агент увидит как приём из UART, а
 * прочитанное из него — как то, что агент отправил в порт.
 */
class FakeUart
{
public:
    FakeUart()
    {
        m_master = ::posix_openpt(O_RDWR | O_NOCTTY);
        EXPECT_GE(m_master, 0);
        if (m_master < 0)
            return;
        // Без этого дочерний процесс агента унаследовал бы ведущий конец, и закрытие его в
        // тесте ничего бы не обрывало: пара жива, пока открыт хоть один дескриптор.
        ::fcntl(m_master, F_SETFD, FD_CLOEXEC);
        EXPECT_EQ(::grantpt(m_master), 0);
        EXPECT_EQ(::unlockpt(m_master), 0);
        m_slavePath = QString::fromLocal8Bit(::ptsname(m_master));
        ::fcntl(m_master, F_SETFL, ::fcntl(m_master, F_GETFL) | O_NONBLOCK);
        // Свой дескриптор ведомого конца: на Linux чтение ведущего даёт EIO, пока ведомый
        // не открыт ни у кого, а заодно через него тест читает termios, выставленный агентом.
        m_slave = ::open(m_slavePath.toLocal8Bit().constData(), O_RDWR | O_NOCTTY);
        EXPECT_GE(m_slave, 0);
        ::fcntl(m_slave, F_SETFD, FD_CLOEXEC);
    }

    ~FakeUart()
    {
        closeMaster();
        if (m_slave >= 0)
            ::close(m_slave);
    }

    FakeUart(const FakeUart &) = delete;
    FakeUart &operator=(const FakeUart &) = delete;

    /// \brief Путь, который агент откроет как порт.
    QString path() const { return m_slavePath; }

    /// \brief «Выдернуть USB»: закрыть ведущий конец, агент увидит обрыв порта.
    void closeMaster()
    {
        if (m_master >= 0)
            ::close(m_master);
        m_master = -1;
    }

    /// \brief Передать агенту байты, будто они пришли из устройства.
    void emitBytes(const QByteArray &data)
    {
        qsizetype offset = 0;
        while (offset < data.size()) {
            const ssize_t written = ::write(m_master, data.constData() + offset,
                                            size_t(data.size() - offset));
            if (written > 0) {
                offset += written;
            } else if (written < 0 && (errno == EAGAIN || errno == EINTR)) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            } else {
                ADD_FAILURE() << "write to the pty master failed: " << errno;
                return;
            }
        }
    }

    /// \brief Дождаться, пока агент отправит в порт хотя бы `size` байт.
    QByteArray receiveBytes(qsizetype size, int timeoutMs = 8000)
    {
        QByteArray result;
        waitFor(
            [&] {
                char buffer[4096];
                for (;;) {
                    const ssize_t got = ::read(m_master, buffer, sizeof buffer);
                    if (got <= 0)
                        break;
                    result.append(buffer, qsizetype(got));
                }
                return result.size() >= size;
            },
            timeoutMs);
        return result;
    }

    /// \brief Настройки порта, какими их оставил агент.
    bool termios(struct termios *out) const { return ::tcgetattr(m_slave, out) == 0; }

private:
    int m_master = -1;
    int m_slave = -1;
    QString m_slavePath;
};

/// \brief Свободный TCP-порт на петле. Небольшая гонка неизбежна, но на тестовой машине
///        терпима: порт выпускается и тут же отдаётся агенту.
inline quint16 freePort()
{
    QTcpServer probe;
    EXPECT_TRUE(probe.listen(QHostAddress::LocalHost, 0));
    return probe.serverPort();
}

/**
 * \class Agent
 * \brief Запущенный процесс `spotty-uartd` и накопленный им журнал.
 */
class Agent
{
public:
    explicit Agent(const QStringList &arguments)
    {
        m_process.setProgram(QStringLiteral(SPOTTY_UARTD_PATH));
        m_process.setArguments(arguments);
        m_process.start();
        EXPECT_TRUE(m_process.waitForStarted(3000));
    }

    ~Agent()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.terminate();
            if (!m_process.waitForFinished(2000)) {
                m_process.kill();
                m_process.waitForFinished(2000);
            }
        }
    }

    Agent(const Agent &) = delete;
    Agent &operator=(const Agent &) = delete;

    /// \brief Всё, что агент написал в stderr к этому моменту.
    QString log()
    {
        m_log += QString::fromUtf8(m_process.readAllStandardError());
        return m_log;
    }

    /// \brief Дождаться строки в журнале.
    bool waitForLog(const QString &text, int timeoutMs = 10000)
    {
        return waitFor([&] { return log().contains(text); }, timeoutMs);
    }

    /// \brief Агент слушает порт и открыл UART: дальше можно подключаться.
    ///
    /// Сроки ожидания здесь с запасом: набор гоняется параллельно, и на загруженной машине запуск
    /// процесса, а то и просто его первый шаг, иногда занимает секунды.
    bool waitReady() { return waitForLog(QStringLiteral("serial port opened")); }

    /// \brief Дождаться завершения. \return `false`, если не завершился в срок.
    bool waitForExit(int timeoutMs = 10000)
    {
        return waitFor([&] { return m_process.state() == QProcess::NotRunning; }, timeoutMs);
    }

    int exitCode() const { return m_process.exitCode(); }
    qint64 pid() const { return m_process.processId(); }

    /// \brief Запустить, дождаться конца и вернуть код возврата и stderr.
    static int runToEnd(const QStringList &arguments, QString *stderrText, QString *stdoutText = nullptr)
    {
        QProcess process;
        process.setProgram(QStringLiteral(SPOTTY_UARTD_PATH));
        process.setArguments(arguments);
        process.start();
        EXPECT_TRUE(process.waitForFinished(5000));
        if (stderrText)
            *stderrText = QString::fromUtf8(process.readAllStandardError());
        if (stdoutText)
            *stdoutText = QString::fromUtf8(process.readAllStandardOutput());
        return process.exitCode();
    }

private:
    QProcess m_process;
    QString m_log;
};

/// \brief Клиент агента: сокет и всё, что по нему пришло.
struct Client
{
    explicit Client(quint16 port, int receiveBuffer = 0, bool reads = true)
    {
        if (receiveBuffer > 0) {
            // Буфер приёма задаётся до подключения, иначе ядро уже согласовало окно.
            socket.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, receiveBuffer);
        }
        if (reads) {
            QObject::connect(&socket, &QTcpSocket::readyRead, &socket,
                             [this] { received.append(socket.readAll()); });
        } else {
            // QTcpSocket вычитывает данные из ядра сам, даже без единого обработчика. Чтобы
            // клиент действительно не читал, его внутренний буфер ограничен, и после
            // заполнения Qt перестаёт забирать байты из сокета.
            socket.setReadBufferSize(1);
        }
        socket.connectToHost(QHostAddress::LocalHost, port);
        connected = socket.waitForConnected(2000);
    }

    /// \brief Закрыл ли агент соединение со своей стороны.
    bool wasDisconnected()
    {
        return waitFor([&] { return socket.state() == QAbstractSocket::UnconnectedState; }, 8000);
    }

    bool waitForBytes(qsizetype size, int timeoutMs = 8000)
    {
        return waitFor([&] { return received.size() >= size; }, timeoutMs);
    }

    QTcpSocket socket;
    QByteArray received;
    bool connected = false;
};

/// \brief Типовые аргументы: порт на петле и заданный UART.
inline QStringList baseArgs(const FakeUart &uart, quint16 port)
{
    return {QStringLiteral("--device"), uart.path(), QStringLiteral("--port"),
            QString::number(port), QStringLiteral("--bind"), QStringLiteral("127.0.0.1")};
}


} // namespace spotty::test
