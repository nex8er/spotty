/**
 * \file test_uartd.cpp
 * \brief Тесты агента удалённого хоста `spotty-uartd` (agent/spotty-uartd.c).
 *
 * Агент запускается настоящим процессом, как на плате, а UART ему подменяет псевдотерминал:
 * ведомый конец агент открывает как последовательный порт, ведущий держит тест. Так
 * проверяется именно то, что уйдёт в бинарники под все архитектуры, а не модель агента.
 */
#include "support/UartdSupport.h"

using namespace spotty::test;

namespace {

/**
 * \brief Сохраняет ли псевдотерминал размер символа и чётность, которые выставил агент.
 *
 * Драйвер pty в Linux (pty_set_termios) всегда оставляет 8 бит без чётности, что бы ни просили, поэтому
 * проверить эти два признака через псевдотерминал можно только на macOS. Скорость, нечётность и стоп-биты
 * сохраняются везде. Разбор формата в агенте от этого не страдает: он проверяется на macOS и в
 * `--check` без порта.
 */
#ifdef __linux__
constexpr bool kPtyKeepsCharacterFormat = false;
#else
constexpr bool kPtyKeepsCharacterFormat = true;
#endif

} // namespace

TEST(Uartd, RelaysBothDirectionsByteForByte)
{
    FakeUart uart;
    const quint16 port = freePort();
    Agent agent(baseArgs(uart, port));
    ASSERT_TRUE(agent.waitReady()) << qPrintable(agent.log());

    Client client(port);
    ASSERT_TRUE(client.connected);
    ASSERT_TRUE(agent.waitForLog(QStringLiteral("connected (1 total)")));

    uart.emitBytes(everyByte());
    ASSERT_TRUE(client.waitForBytes(256));
    EXPECT_EQ(client.received, everyByte());

    client.socket.write(everyByte());
    client.socket.flush();
    EXPECT_EQ(uart.receiveBytes(256), everyByte());
}

TEST(Uartd, BroadcastsToEveryClientAndTakesInputFromAny)
{
    FakeUart uart;
    const quint16 port = freePort();
    Agent agent(baseArgs(uart, port));
    ASSERT_TRUE(agent.waitReady());

    Client first(port);
    Client second(port);
    ASSERT_TRUE(first.connected && second.connected);
    ASSERT_TRUE(agent.waitForLog(QStringLiteral("connected (2 total)")));

    uart.emitBytes("hello\r\n");
    ASSERT_TRUE(first.waitForBytes(7));
    ASSERT_TRUE(second.waitForBytes(7));
    EXPECT_EQ(first.received, QByteArray("hello\r\n"));
    EXPECT_EQ(second.received, QByteArray("hello\r\n"));

    // Запись одного клиента клиентам не возвращается: это не эхо, а байты для устройства.
    first.socket.write("from-first");
    first.socket.flush();
    EXPECT_EQ(uart.receiveBytes(10), QByteArray("from-first"));
    second.socket.write("from-second");
    second.socket.flush();
    EXPECT_EQ(uart.receiveBytes(11), QByteArray("from-second"));
    EXPECT_EQ(first.received, QByteArray("hello\r\n"));
}

TEST(Uartd, AppliesBaudAndFrameFormatToThePort)
{
    FakeUart uart;
    Agent agent(baseArgs(uart, freePort())
                + QStringList{QStringLiteral("--baud"), QStringLiteral("9600"),
                              QStringLiteral("--format"), QStringLiteral("7E2")});
    ASSERT_TRUE(agent.waitReady()) << qPrintable(agent.log());

    struct termios tio;
    ASSERT_TRUE(uart.termios(&tio));
    EXPECT_EQ(cfgetispeed(&tio), speed_t(B9600));
    EXPECT_EQ(cfgetospeed(&tio), speed_t(B9600));
    if (kPtyKeepsCharacterFormat) {
        EXPECT_EQ(tio.c_cflag & CSIZE, tcflag_t(CS7));
        EXPECT_TRUE(tio.c_cflag & PARENB);
    }
    EXPECT_FALSE(tio.c_cflag & PARODD);
    EXPECT_TRUE(tio.c_cflag & CSTOPB);
    // Сырой режим: иначе драйвер терминала переводил бы строки и отражал ввод обратно.
    EXPECT_FALSE(tio.c_lflag & ECHO);
    EXPECT_FALSE(tio.c_lflag & ICANON);
    EXPECT_FALSE(tio.c_oflag & OPOST);
}

TEST(Uartd, AllowListLetsMatchingClientsIn)
{
    FakeUart uart;
    const quint16 port = freePort();
    Agent agent(baseArgs(uart, port)
                + QStringList{QStringLiteral("--allow"),
                              QStringLiteral("10.0.0.0/8, 127.0.0.0/8")});
    ASSERT_TRUE(agent.waitReady());

    Client client(port);
    ASSERT_TRUE(client.connected);
    ASSERT_TRUE(agent.waitForLog(QStringLiteral("connected (1 total)")));
    uart.emitBytes("x");
    EXPECT_TRUE(client.waitForBytes(1));
}

TEST(Uartd, AllowListRejectsEveryoneElse)
{
    FakeUart uart;
    const quint16 port = freePort();
    // Ни диапазон 10.0.0.0/8, ни чужой одиночный адрес, ни IPv6 клиента с 127.0.0.1 не пускают.
    Agent agent(baseArgs(uart, port)
                + QStringList{QStringLiteral("--allow"),
                              QStringLiteral("10.0.0.0/8,192.168.1.5,::1")});
    ASSERT_TRUE(agent.waitReady());

    Client client(port);
    ASSERT_TRUE(client.connected); // ядро соединение принимает, решает уже агент
    EXPECT_TRUE(client.wasDisconnected());
    EXPECT_TRUE(agent.waitForLog(QStringLiteral("not in the allow list")));

    uart.emitBytes("secret");
    EXPECT_TRUE(client.received.isEmpty());
}

TEST(Uartd, DisconnectsClientsWhenUartDisappearsAndServesAgainWhenItReturns)
{
    TempDir dir;
    const QString link = dir.filePath(QStringLiteral("uart"));

    auto first = std::make_unique<FakeUart>();
    ASSERT_TRUE(QFile::link(first->path(), link));

    const quint16 port = freePort();
    Agent agent({QStringLiteral("--device"), link, QStringLiteral("--port"),
                 QString::number(port), QStringLiteral("--bind"), QStringLiteral("127.0.0.1")});
    ASSERT_TRUE(agent.waitReady());

    Client client(port);
    ASSERT_TRUE(client.connected);
    ASSERT_TRUE(agent.waitForLog(QStringLiteral("connected (1 total)")));

    // «Выдернули USB»: клиента держать нельзя, по такому каналу ничего больше не придёт.
    first->closeMaster();
    EXPECT_TRUE(client.wasDisconnected()) << qPrintable(agent.log());
    EXPECT_TRUE(agent.waitForLog(QStringLiteral("serial port lost"))) << qPrintable(agent.log());

    // Пока порта нет, соединение принимается и тут же закрывается.
    Client during(port);
    EXPECT_TRUE(during.wasDisconnected());
    EXPECT_TRUE(agent.waitForLog(QStringLiteral("serial port is not available")));

    // Адаптер вернулся (под другим узлом: ссылка теперь указывает на новый псевдотерминал).
    FakeUart second;
    QFile::remove(link);
    ASSERT_TRUE(QFile::link(second.path(), link));
    ASSERT_TRUE(waitFor(
        [&] { return agent.log().count(QStringLiteral("serial port opened")) == 2; }, 5000));

    // Строка «connected (1 total)» уже есть от первого клиента: ждать нужно новой, иначе байты уйдут
    // раньше, чем агент примет клиента, — нулю клиентов, и тест падает через раз на медленной машине.
    const auto connectedLines = [&] { return agent.log().count(QStringLiteral("connected (1 total)")); };
    const int connectedBefore = connectedLines();
    Client after(port);
    ASSERT_TRUE(after.connected);
    ASSERT_TRUE(waitFor([&] { return connectedLines() > connectedBefore; }, 10000));
    second.emitBytes("back");
    EXPECT_TRUE(after.waitForBytes(4));
    EXPECT_EQ(after.received, QByteArray("back"));
}

TEST(Uartd, StartsWithoutThePortAndWaitsForIt)
{
    TempDir dir;
    const quint16 port = freePort();
    Agent agent({QStringLiteral("--device"), dir.filePath(QStringLiteral("missing")),
                 QStringLiteral("--port"), QString::number(port), QStringLiteral("--bind"),
                 QStringLiteral("127.0.0.1")});
    // На загрузке системы USB-адаптер часто появляется позже сервиса: это не повод падать.
    ASSERT_TRUE(agent.waitForLog(QStringLiteral("listening")));
    // Не «уже есть в журнале», а «появится»: строка о порте идёт следом за строкой о прослушивании.
    EXPECT_TRUE(agent.waitForLog(QStringLiteral("cannot open")));

    Client client(port);
    ASSERT_TRUE(client.connected);
    EXPECT_TRUE(client.wasDisconnected());
}

TEST(Uartd, RefusesTheSeventeenthClient)
{
    FakeUart uart;
    const quint16 port = freePort();
    Agent agent(baseArgs(uart, port));
    ASSERT_TRUE(agent.waitReady());

    std::vector<std::unique_ptr<Client>> clients;
    for (int i = 0; i < 16; ++i) {
        clients.push_back(std::make_unique<Client>(port));
        ASSERT_TRUE(clients.back()->connected);
        ASSERT_TRUE(agent.waitForLog(QStringLiteral("connected (%1 total)").arg(i + 1)));
    }

    Client extra(port);
    ASSERT_TRUE(extra.connected);
    EXPECT_TRUE(extra.wasDisconnected());
    EXPECT_TRUE(agent.waitForLog(QStringLiteral("too many clients")));
}

TEST(Uartd, DropsAClientThatDoesNotReadWithoutHoldingBackTheOthers)
{
    FakeUart uart;
    const quint16 port = freePort();
    Agent agent(baseArgs(uart, port));
    ASSERT_TRUE(agent.waitReady());

    // Молчун не читает вовсе (без обработчика readyRead данные остаются в буфере сокета) и
    // держит крошечный буфер приёма: иначе ядро спрятало бы в сокетах мегабайты, и до
    // предела агента дело дошло бы не скоро.
    Client reader(port);
    Client mute(port, 4096, false);
    ASSERT_TRUE(reader.connected && mute.connected);
    ASSERT_TRUE(agent.waitForLog(QStringLiteral("connected (2 total)")));

    QByteArray chunk(16 * 1024, '\0');
    for (int i = 0; i < chunk.size(); ++i)
        chunk[i] = char(i * 7);

    // 24 МиБ: с запасом больше буферов ядра (до нескольких МиБ) и предела агента (1 МиБ).
    constexpr int kChunks = 24 * 64;
    qint64 sent = 0;
    for (int i = 0; i < kChunks; ++i) {
        uart.emitBytes(chunk);
        sent += chunk.size();
        if (i % 16 == 0)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }

    EXPECT_TRUE(agent.waitForLog(QStringLiteral("backlog limit"), 20000)) << qPrintable(agent.log());
    // Сосед получил поток целиком и в порядке отправки: отключение молчуна его не задело.
    ASSERT_TRUE(waitFor([&] { return reader.received.size() >= sent; }, 20000));
    EXPECT_EQ(reader.received.size(), sent);
    for (int i = 0; i < kChunks; ++i)
        ASSERT_EQ(reader.received.mid(qsizetype(i) * chunk.size(), chunk.size()), chunk) << i;
    EXPECT_EQ(reader.socket.state(), QAbstractSocket::ConnectedState);
}

TEST(Uartd, ReadsTheConfigFileAndLetsTheCommandLineOverrideIt)
{
    FakeUart uart;
    TempDir dir;
    const quint16 configured = freePort();
    const quint16 overriding = freePort();

    QFile file(dir.filePath(QStringLiteral("7777.conf")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QStringLiteral("# написано установщиком\n"
                              "device = %1\n"
                              "baud=19200\n"
                              "format = 8O1\n"
                              "port=%2\n"
                              "bind=127.0.0.1\n")
                   .arg(uart.path())
                   .arg(configured)
                   .toUtf8());
    file.close();

    // Параметр командной строки сильнее файла, в каком бы порядке ни стояли аргументы.
    Agent agent({QStringLiteral("--port"), QString::number(overriding), QStringLiteral("--config"),
                 file.fileName()});
    ASSERT_TRUE(agent.waitReady()) << qPrintable(agent.log());

    struct termios tio;
    ASSERT_TRUE(uart.termios(&tio));
    EXPECT_EQ(cfgetispeed(&tio), speed_t(B19200));
    if (kPtyKeepsCharacterFormat)
        EXPECT_TRUE(tio.c_cflag & PARENB);
    EXPECT_TRUE(tio.c_cflag & PARODD);

    Client onOverride(overriding);
    EXPECT_TRUE(onOverride.connected);
    Client onConfigured(configured);
    EXPECT_FALSE(onConfigured.connected);
}

TEST(Uartd, RejectsBadSettingsWithExitCodeTwoAndTheReason)
{
    struct Case
    {
        QStringList arguments;
        QString expectedMessage;
    };
    const Case cases[] = {
        {{"--check", "--device", "/dev/x", "--format", "9N1"}, "bad format '9N1'"},
        {{"--check", "--device", "/dev/x", "--format", "8X1"}, "bad format '8X1'"},
        {{"--check", "--device", "/dev/x", "--baud", "fast"}, "unsupported baud rate 'fast'"},
        {{"--check", "--device", "/dev/x", "--baud", "0"}, "unsupported baud rate '0'"},
        {{"--check", "--device", "/dev/x", "--port", "70000"}, "bad port '70000'"},
        {{"--check", "--device", "/dev/x", "--port", "0"}, "bad port '0'"},
        {{"--check", "--device", "/dev/x", "--allow", "10.0.0.0/40"}, "bad allow rule"},
        {{"--check", "--device", "/dev/x", "--allow", "not-an-address"}, "bad allow rule"},
        {{"--check", "--device", "/dev/x", "--flow", "magic"}, "bad flow 'magic'"},
        {{"--check", "--device", "/dev/x", "--nonsense", "1"}, "unknown option 'nonsense'"},
        {{"--check"}, "no serial port given"},
        {{"--check", "--device"}, "needs a value"},
        {{"--check", "stray"}, "unexpected argument 'stray'"},
        {{"--config", "/nonexistent/spotty.conf"}, "cannot read"},
    };
    for (const Case &testCase : cases) {
        QString error;
        EXPECT_EQ(Agent::runToEnd(testCase.arguments, &error), 2) << qPrintable(error);
        EXPECT_TRUE(error.contains(testCase.expectedMessage))
            << qPrintable(testCase.arguments.join(' ')) << " -> " << qPrintable(error);
    }
}

TEST(Uartd, NamesTheLineOfAnUnknownKeyInTheConfigFile)
{
    // Опечатка в конфиге обязана быть ошибкой, а не работой на умолчаниях: по поведению её не
    // заметить, а искать придётся вслепую.
    TempDir dir;
    QFile file(dir.filePath(QStringLiteral("bad.conf")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("device=/dev/x\n\nbuad=9600\n");
    file.close();

    QString error;
    EXPECT_EQ(Agent::runToEnd({QStringLiteral("--check"), QStringLiteral("--config"),
                               file.fileName()},
                              &error),
              2);
    EXPECT_TRUE(error.contains(QStringLiteral("bad.conf:3")));
    EXPECT_TRUE(error.contains(QStringLiteral("unknown option 'buad'")));
}

TEST(Uartd, AcceptsAValidConfigurationOnCheck)
{
    QString error;
    EXPECT_EQ(Agent::runToEnd({QStringLiteral("--check"), QStringLiteral("--device"),
                               QStringLiteral("/dev/x"), QStringLiteral("--baud"),
                               QStringLiteral("921600"), QStringLiteral("--format"),
                               QStringLiteral("8n2"), QStringLiteral("--allow"),
                               QStringLiteral("192.168.0.0/16,fe80::/10,::1")},
                              &error),
              0)
        << qPrintable(error);
}

TEST(Uartd, ReportsItsVersion)
{
    QString error;
    QString output;
    EXPECT_EQ(Agent::runToEnd({QStringLiteral("--version")}, &error, &output), 0);
    EXPECT_TRUE(output.startsWith(QStringLiteral("spotty-uartd ")));
}

TEST(Uartd, ScanPrintsThreeTabSeparatedColumns)
{
    // Что именно найдётся, зависит от машины (на macOS и в контейнере — ничего), поэтому
    // проверяется форма: её разбирает Spotty, не выбрасывая пустые поля.
    QString error;
    QString output;
    ASSERT_EQ(Agent::runToEnd({QStringLiteral("--scan")}, &error, &output), 0)
        << qPrintable(error);
    for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        EXPECT_EQ(line.count(QLatin1Char('\t')), 2) << qPrintable(line);
        EXPECT_TRUE(line.startsWith(QStringLiteral("/dev/"))) << qPrintable(line);
    }
}

TEST(Uartd, SecondInstanceOnTheSamePortFailsAtStartup)
{
    FakeUart first;
    FakeUart second;
    const quint16 port = freePort();
    Agent running(baseArgs(first, port));
    ASSERT_TRUE(running.waitReady());

    // Занятый порт — ошибка настройки: о ней узнают при запуске, а не когда появится UART.
    Agent duplicate(baseArgs(second, port));
    ASSERT_TRUE(duplicate.waitForExit());
    EXPECT_EQ(duplicate.exitCode(), 1);
    EXPECT_TRUE(duplicate.log().contains(QStringLiteral("cannot listen")));
}

TEST(Uartd, StopsCleanlyOnTerminateAndRemovesItsPidFile)
{
    FakeUart uart;
    TempDir dir;
    const QString pidFile = dir.filePath(QStringLiteral("agent.pid"));
    Agent agent(baseArgs(uart, freePort()) + QStringList{QStringLiteral("--pidfile"), pidFile});
    ASSERT_TRUE(agent.waitReady());

    QFile file(pidFile);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll().trimmed().toLongLong(), agent.pid());
    file.close();

    ::kill(pid_t(agent.pid()), SIGTERM);
    ASSERT_TRUE(agent.waitForExit());
    EXPECT_EQ(agent.exitCode(), 0);
    EXPECT_TRUE(agent.log().contains(QStringLiteral("stopping")));
    EXPECT_FALSE(QFile::exists(pidFile));
}
