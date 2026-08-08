/**
 * \file test_data_filter.cpp
 * \brief Тесты цепочки преобразования потока в spotty::Session.
 */
#include "support/FakeInterfacePlugin.h"
#include "support/TestSupport.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <Session.h>
#include <settings/SettingsStore.h>

#include <spotty/api/IDataFilter.h>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::FakeChannel;
using spotty::test::FakeInterfacePlugin;
using spotty::test::TempDir;
using spotty::test::waitFor;

namespace {

const QString kDeviceId = QStringLiteral("fake:a");

/**
 * \struct Fixture
 * \brief Обвязка «плагин + реестр + сессия» с одним открытым устройством.
 *
 * Повторяет обвязку из test_session.cpp намеренно: связывать два набора общим заголовком
 * значило бы, что правка ради одного молча меняет условия другого.
 */
struct Fixture
{
    TempDir dir;
    PluginManager plugins;
    FakeInterfacePlugin plugin;
    SettingsStore store;
    InterfaceRegistry registry;
    Session session;

    Fixture()
        : store(dir.filePath(QStringLiteral("interfaces.json")))
        , registry(&plugins, &store)
        , session(&plugins, &registry)
    {
        EXPECT_TRUE(plugins.addPlugin(&plugin));
        store.load();
        plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                          QStringLiteral("dev-a"))};
        registry.refresh();
    }

    bool openDevice()
    {
        session.setInterfaceId(kDeviceId);
        session.open();
        return waitFor([this] { return session.state() == ChannelState::Open; });
    }

    /// \brief Изобразить приход данных и дождаться появления строки в буфере.
    bool receive(const QByteArray &data)
    {
        FakeChannel *channel = plugin.lastChannel;
        if (!channel)
            return false;

        const qint64 before = session.buffer()->nextLineNumber();
        QMetaObject::invokeMethod(channel, "injectData", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, data));
        return waitFor([&] { return session.buffer()->nextLineNumber() > before; });
    }

    /// \brief Изобразить приход данных, ничего не ожидая: порция может быть проглочена.
    void receiveWithoutWaiting(const QByteArray &data)
    {
        FakeChannel *channel = plugin.lastChannel;
        ASSERT_NE(channel, nullptr);
        QMetaObject::invokeMethod(channel, "injectData", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, data));
    }

    /// \brief Текст последней строки терминала.
    QString lastLine() const
    {
        const qint64 next = session.buffer()->nextLineNumber();
        if (next == 0)
            return {};
        const TerminalBuffer::Line *line = session.buffer()->line(next - 1);
        return line ? line->text : QString();
    }
};

/// \brief Звено, переводящее принятое в верхний регистр.
class UppercaseFilter : public IDataFilter
{
public:
    QByteArray filterIncoming(const QByteArray &data, qint64) override
    {
        return data.toUpper();
    }
};

/// \brief Звено, проглатывающее всё в обоих направлениях.
class SwallowFilter : public IDataFilter
{
public:
    QByteArray filterIncoming(const QByteArray &, qint64) override { return {}; }
    QByteArray filterOutgoing(const QByteArray &) override { return {}; }
};

/// \brief Звено, дописывающее свою метку и запоминающее порядок вызовов.
class TagFilter : public IDataFilter
{
public:
    TagFilter(QByteArray tag, QStringList *incomingOrder, QStringList *outgoingOrder)
        : m_tag(std::move(tag))
        , m_incoming(incomingOrder)
        , m_outgoing(outgoingOrder)
    {
    }

    QByteArray filterIncoming(const QByteArray &data, qint64) override
    {
        if (m_incoming)
            m_incoming->append(QString::fromLatin1(m_tag));
        return data + m_tag;
    }

    QByteArray filterOutgoing(const QByteArray &data) override
    {
        if (m_outgoing)
            m_outgoing->append(QString::fromLatin1(m_tag));
        return data + m_tag;
    }

private:
    QByteArray m_tag;
    QStringList *m_incoming = nullptr;
    QStringList *m_outgoing = nullptr;
};

/**
 * \brief Звено, копящее байты до перевода строки.
 *
 * Проверяет главное правило отметки времени: придержанное отдаётся с временем той порции,
 * в которой кадр завершился, а не той, в которой начался.
 */
class BufferingFilter : public IDataFilter
{
public:
    QByteArray filterIncoming(const QByteArray &data, qint64 monotonicNs) override
    {
        m_pending += data;
        if (!m_pending.contains('\n'))
            return {};

        lastReleaseNs = monotonicNs;
        QByteArray out;
        out.swap(m_pending);
        return out;
    }

    /// \brief Отметка, с которой звено выпустило накопленное.
    qint64 lastReleaseNs = -1;

private:
    QByteArray m_pending;
};

/**
 * \struct Recorder
 * \brief Записыватель сигнала.
 *
 * Заменяет QSignalSpy, который живёт в Qt6::Test: набор построен на GoogleTest, и тащить
 * ради наблюдения за одним сигналом второй тестовый фреймворк незачем.
 */
struct Recorder
{
    QList<QByteArray> data;
    QList<qint64> stamps;
    int count = 0;
};

} // namespace

TEST(DataFilter, TransformsIncomingBeforeTheBuffer)
{
    Fixture fixture;
    UppercaseFilter filter;
    fixture.session.addDataFilter(&filter, 100, QStringLiteral("upper"));

    ASSERT_TRUE(fixture.openDevice());
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("hello\n")));

    EXPECT_EQ(fixture.lastLine(), QStringLiteral("HELLO"));
}

TEST(DataFilter, DataLoggedStaysRaw)
{
    Fixture fixture;
    UppercaseFilter filter;
    fixture.session.addDataFilter(&filter, 100, QStringLiteral("upper"));

    ASSERT_TRUE(fixture.openDevice());
    Recorder logged;
    QObject::connect(&fixture.session, &Session::dataLogged, &fixture.session,
                     [&logged](const QByteArray &bytes, DataDirection) {
                         logged.data.append(bytes);
                     });
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("hello\n")));

    ASSERT_FALSE(logged.data.isEmpty());
    // Журнал обязан получить провод, а не результат работы плагинов.
    EXPECT_EQ(logged.data.first(), QByteArrayLiteral("hello\n"));
}

TEST(DataFilter, SwallowedChunkNeverReachesTheBuffer)
{
    Fixture fixture;
    SwallowFilter filter;
    fixture.session.addDataFilter(&filter, 100, QStringLiteral("swallow"));

    ASSERT_TRUE(fixture.openDevice());
    Recorder logged;
    QObject::connect(&fixture.session, &Session::dataLogged, &fixture.session,
                     [&logged](const QByteArray &, DataDirection) { ++logged.count; });
    const qint64 before = fixture.session.buffer()->nextLineNumber();

    fixture.receiveWithoutWaiting(QByteArrayLiteral("hello\n"));
    // Ждать появления строки нельзя — её не будет; ждём того, что точно происходит.
    ASSERT_TRUE(waitFor([&] { return logged.count > 0; }));

    EXPECT_EQ(fixture.session.buffer()->nextLineNumber(), before);
}

TEST(DataFilter, IncomingOrderFollowsOrderAndName)
{
    Fixture fixture;
    QStringList incoming;
    TagFilter first(QByteArrayLiteral("1"), &incoming, nullptr);
    TagFilter second(QByteArrayLiteral("2"), &incoming, nullptr);
    TagFilter third(QByteArrayLiteral("3"), &incoming, nullptr);

    // Регистрируем вразнобой: порядок обязан задаваться числом и именем, а не тем, кто
    // успел раньше. Второе и третье звено делят order, их разводит имя.
    fixture.session.addDataFilter(&third, 200, QStringLiteral("b"));
    fixture.session.addDataFilter(&first, 100, QStringLiteral("z"));
    fixture.session.addDataFilter(&second, 200, QStringLiteral("a"));

    ASSERT_TRUE(fixture.openDevice());
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("x\n")));

    EXPECT_EQ(incoming, QStringList({QStringLiteral("1"), QStringLiteral("2"),
                                     QStringLiteral("3")}));
}

TEST(DataFilter, OutgoingOrderIsTheReverseOfIncoming)
{
    Fixture fixture;
    QStringList incoming;
    QStringList outgoing;
    TagFilter first(QByteArrayLiteral("1"), &incoming, &outgoing);
    TagFilter second(QByteArrayLiteral("2"), &incoming, &outgoing);

    fixture.session.addDataFilter(&first, 100, QStringLiteral("a"));
    fixture.session.addDataFilter(&second, 200, QStringLiteral("b"));

    ASSERT_TRUE(fixture.openDevice());
    fixture.session.send(QByteArrayLiteral("cmd"));

    FakeChannel *channel = fixture.plugin.lastChannel;
    ASSERT_NE(channel, nullptr);
    ASSERT_TRUE(waitFor([&] { return !channel->written.isEmpty(); }));

    EXPECT_EQ(outgoing, QStringList({QStringLiteral("2"), QStringLiteral("1")}));
    EXPECT_EQ(channel->written, QByteArrayLiteral("cmd21"));
}

TEST(DataFilter, SwallowedOutgoingNeverReachesTheChannel)
{
    Fixture fixture;
    SwallowFilter filter;
    fixture.session.addDataFilter(&filter, 100, QStringLiteral("swallow"));

    ASSERT_TRUE(fixture.openDevice());
    Recorder errors;
    QObject::connect(&fixture.session, &Session::errorOccurred, &fixture.session,
                     [&errors](const QString &) { ++errors.count; });
    fixture.session.send(QByteArrayLiteral("cmd"));

    FakeChannel *channel = fixture.plugin.lastChannel;
    ASSERT_NE(channel, nullptr);
    // Дать очереди потока ввода-вывода шанс доставить запись, если бы она была.
    EXPECT_FALSE(waitFor([&] { return !channel->written.isEmpty(); }, 200));
    // Отказ звена отправлять — не ошибка и жаловаться на неё некому.
    EXPECT_EQ(errors.count, 0);
}

TEST(DataFilter, RemovedFilterStopsAffectingTheStream)
{
    Fixture fixture;
    UppercaseFilter filter;
    fixture.session.addDataFilter(&filter, 100, QStringLiteral("upper"));

    ASSERT_TRUE(fixture.openDevice());
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("one\n")));
    EXPECT_EQ(fixture.lastLine(), QStringLiteral("ONE"));

    fixture.session.removeDataFilter(&filter);
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("two\n")));
    EXPECT_EQ(fixture.lastLine(), QStringLiteral("two"));
}

TEST(DataFilter, AddingTheSameFilterTwiceKeepsOneLink)
{
    Fixture fixture;
    QStringList incoming;
    TagFilter tag(QByteArrayLiteral("t"), &incoming, nullptr);

    fixture.session.addDataFilter(&tag, 100, QStringLiteral("a"));
    fixture.session.addDataFilter(&tag, 500, QStringLiteral("b"));

    ASSERT_TRUE(fixture.openDevice());
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("x\n")));

    EXPECT_EQ(incoming.size(), 1);
}

TEST(DataFilter, HeldBytesAreReleasedWithTheTimestampOfTheClosingChunk)
{
    Fixture fixture;
    BufferingFilter filter;
    fixture.session.addDataFilter(&filter, 100, QStringLiteral("buffer"));

    ASSERT_TRUE(fixture.openDevice());
    FakeChannel *channel = fixture.plugin.lastChannel;
    ASSERT_NE(channel, nullptr);

    // Первая порция без перевода строки: звено её придерживает, в терминал не идёт ничего.
    channel->monotonicNs = 1000;
    const qint64 before = fixture.session.buffer()->nextLineNumber();
    fixture.receiveWithoutWaiting(QByteArrayLiteral("par"));
    EXPECT_FALSE(waitFor([&] { return fixture.session.buffer()->nextLineNumber() > before; },
                         200));

    // Вторая завершает кадр. Отметкой становится её время — время последнего байта.
    channel->monotonicNs = 5000;
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("tial\n")));

    EXPECT_EQ(fixture.lastLine(), QStringLiteral("partial"));
    EXPECT_EQ(filter.lastReleaseNs, 5000);
}

TEST(DataFilter, DataReceivedCarriesTheChannelTimestamp)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());

    FakeChannel *channel = fixture.plugin.lastChannel;
    ASSERT_NE(channel, nullptr);
    channel->monotonicNs = 4242;

    Recorder received;
    QObject::connect(&fixture.session, &Session::dataReceived, &fixture.session,
                     [&received](const QByteArray &bytes, qint64 monotonicNs) {
                         received.data.append(bytes);
                         received.stamps.append(monotonicNs);
                     });
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("x\n")));

    ASSERT_FALSE(received.data.isEmpty());
    EXPECT_EQ(received.data.first(), QByteArrayLiteral("x\n"));
    EXPECT_EQ(received.stamps.first(), 4242);
}
