/**
 * \file test_session.cpp
 * \brief Тесты spotty::Session.
 */
#include "support/FakeInterfacePlugin.h"
#include "support/TestSupport.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <Session.h>
#include <settings/SettingsStore.h>

#include <QPointer>
#include <QStringList>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::FakeChannel;
using spotty::test::FakeInterfacePlugin;
using spotty::test::TempDir;
using spotty::test::waitFor;

namespace {

/// \brief Идентификатор устройства, с которым работают все тесты этого файла.
const QString kDeviceId = QStringLiteral("fake:a");

/**
 * \struct Fixture
 * \brief Обвязка «плагин + реестр + сессия» с одним доступным устройством.
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

    /// \brief Открыть устройство и дождаться перехода в открытое состояние.
    bool openDevice()
    {
        session.setInterfaceId(kDeviceId);
        session.open();
        return waitFor([this] { return session.state() == ChannelState::Open; });
    }

    /// \brief Изобразить приход данных и дождаться их появления в буфере.
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
};

} // namespace

TEST(Session, StartsClosedAndInactive)
{
    Fixture fixture;

    EXPECT_EQ(fixture.session.state(), ChannelState::Closed);
    EXPECT_FALSE(fixture.session.isActive());
    EXPECT_TRUE(fixture.session.interfaceId().isEmpty());
}

TEST(Session, OpeningWithoutInterfaceReportsError)
{
    Fixture fixture;

    QStringList errors;
    QObject::connect(&fixture.session, &Session::errorOccurred,
                     [&](const QString &message) { errors.append(message); });

    fixture.session.open();

    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(fixture.session.state(), ChannelState::Closed);
}

TEST(Session, OpensSelectedInterface)
{
    Fixture fixture;

    ASSERT_TRUE(fixture.openDevice());
    EXPECT_TRUE(fixture.session.isActive());
    EXPECT_EQ(fixture.session.interfaceId(), kDeviceId);
}

TEST(Session, OpenAnnouncesItselfInTheBuffer)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());

    // Сообщение об открытии — часть журнала: без него непонятно, где начался сеанс.
    ASSERT_GT(fixture.session.buffer()->lineCount(), 0);
    EXPECT_EQ(fixture.session.buffer()->line(0)->direction, DataDirection::System);
}

TEST(Session, ChannelReceivesNormalisedSettings)
{
    Fixture fixture;
    // Записан только один ключ; второй должен прийти из умолчаний схемы.
    fixture.registry.setSettingsFor(kDeviceId,
                                    {{QStringLiteral("speed"), 4800}});

    ASSERT_TRUE(fixture.openDevice());
    ASSERT_NE(fixture.plugin.lastChannel, nullptr);

    EXPECT_EQ(fixture.plugin.lastChannel->lastSettings.value(QStringLiteral("speed")).toInt(),
              4800);
    EXPECT_EQ(fixture.plugin.lastChannel->lastSettings.value(QStringLiteral("mode")).toString(),
              QStringLiteral("8N1"));
}

TEST(Session, MissingRequiredSettingBlocksOpenWithoutTouchingTheChannel)
{
    Fixture fixture;
    fixture.plugin.requireMode = true;
    // "mode" присутствует в сохранённых настройках, но пуст — это именно то, что реестр
    // отдал бы после normalized(), если пользователь стёр значение поля в UI.
    fixture.registry.setSettingsFor(kDeviceId, {{QStringLiteral("mode"), QString()}});

    QStringList missingIds;
    QList<QStringList> missingFields;
    QObject::connect(&fixture.session, &Session::requiredSettingsMissing,
                     [&](const QString &id, const QStringList &fields) {
        missingIds.append(id);
        missingFields.append(fields);
    });

    fixture.session.setInterfaceId(kDeviceId);
    fixture.session.open();

    ASSERT_EQ(missingIds.size(), 1);
    EXPECT_EQ(missingIds.first(), kDeviceId);
    ASSERT_EQ(missingFields.first().size(), 1);
    EXPECT_EQ(missingFields.first().first(), QStringLiteral("mode"));

    // Открытие не должно было даже попытаться создать канал — обязательное поле пусто,
    // значит попытка connect() (аналог JLINK_Connect() у настоящего плагина) заведомо
    // напрасна и не должна происходить вовсе.
    EXPECT_EQ(fixture.plugin.lastChannel, nullptr);
    EXPECT_EQ(fixture.session.state(), ChannelState::Closed);
    EXPECT_FALSE(fixture.session.isActive());
}

TEST(Session, FailedOpenReportsErrorAndState)
{
    Fixture fixture;
    fixture.plugin.failNextOpen = true;

    QStringList errors;
    QObject::connect(&fixture.session, &Session::errorOccurred,
                     [&](const QString &message) { errors.append(message); });

    fixture.session.setInterfaceId(kDeviceId);
    fixture.session.open();

    ASSERT_TRUE(waitFor([&] { return fixture.session.state() == ChannelState::Error; }));
    EXPECT_FALSE(errors.isEmpty());
}

TEST(Session, SendReachesTheChannel)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());

    fixture.session.send(QByteArrayLiteral("AT\r\n"));

    FakeChannel *channel = fixture.plugin.lastChannel;
    ASSERT_NE(channel, nullptr);
    EXPECT_TRUE(waitFor([&] { return channel->written == QByteArrayLiteral("AT\r\n"); }));
}

TEST(Session, SendOnClosedChannelReportsError)
{
    Fixture fixture;

    QStringList errors;
    QObject::connect(&fixture.session, &Session::errorOccurred,
                     [&](const QString &message) { errors.append(message); });

    fixture.session.send(QByteArrayLiteral("data"));

    EXPECT_EQ(errors.size(), 1);
}

TEST(Session, SentDataIsEchoedIntoTheBuffer)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());
    const qint64 before = fixture.session.buffer()->nextLineNumber();

    fixture.session.send(QByteArrayLiteral("ping"));

    ASSERT_TRUE(waitFor([&] {
        return fixture.session.buffer()->nextLineNumber() > before;
    }));
    EXPECT_EQ(fixture.session.buffer()->line(before)->direction, DataDirection::Tx);
}

TEST(Session, EchoCanBeDisabled)
{
    Fixture fixture;
    fixture.session.setEchoEnabled(false);
    ASSERT_TRUE(fixture.openDevice());
    const qint64 before = fixture.session.buffer()->nextLineNumber();

    fixture.session.send(QByteArrayLiteral("ping"));

    FakeChannel *channel = fixture.plugin.lastChannel;
    ASSERT_TRUE(waitFor([&] { return !channel->written.isEmpty(); }));
    // Данные ушли, но в терминале их быть не должно.
    EXPECT_EQ(fixture.session.buffer()->nextLineNumber(), before);
}

TEST(Session, ReceivedDataLandsInTheBuffer)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());
    const qint64 before = fixture.session.buffer()->nextLineNumber();

    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("hello\n")));

    ASSERT_NE(fixture.session.buffer()->line(before), nullptr);
    EXPECT_EQ(fixture.session.buffer()->line(before)->direction, DataDirection::Rx);
    EXPECT_EQ(fixture.session.buffer()->line(before)->text, QStringLiteral("hello"));
}

TEST(Session, SharedBufferKeepsSourceAndEchoForBothInterfaces)
{
    Fixture first;
    Fixture second;
    TerminalBuffer shared;

    first.session.setSharedBuffer(&shared, 0);
    second.session.setSharedBuffer(&shared, 1);
    ASSERT_TRUE(first.openDevice());
    ASSERT_TRUE(second.openDevice());
    shared.clear();

    ASSERT_TRUE(first.receive(QByteArrayLiteral("from-a\n")));
    ASSERT_TRUE(second.receive(QByteArrayLiteral("from-b\n")));
    ASSERT_EQ(shared.lineCount(), 2);
    EXPECT_EQ(shared.line(0)->source, 0);
    EXPECT_EQ(shared.line(0)->text, QStringLiteral("from-a"));
    EXPECT_EQ(shared.line(1)->source, 1);
    EXPECT_EQ(shared.line(1)->text, QStringLiteral("from-b"));

    first.session.send(QByteArrayLiteral("echo-a"));
    ASSERT_TRUE(waitFor([&] { return shared.lineCount() == 3; }));
    EXPECT_EQ(shared.line(2)->direction, DataDirection::Tx);
    EXPECT_EQ(shared.line(2)->source, 0);

    second.session.send(QByteArrayLiteral("echo-b"));
    ASSERT_TRUE(waitFor([&] { return shared.lineCount() == 4; }));
    EXPECT_EQ(shared.line(3)->direction, DataDirection::Tx);
    EXPECT_EQ(shared.line(3)->source, 1);
}

TEST(Session, DataLoggedSignalCarriesRawBytes)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());

    QByteArray logged;
    QObject::connect(&fixture.session, &Session::dataLogged,
                     [&](const QByteArray &data, DataDirection) { logged.append(data); });

    // Журналу поток отдаётся до пакетизации и разбора, ровно как пришёл.
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("\x1b[31mred\n")));

    EXPECT_EQ(logged, QByteArrayLiteral("\x1b[31mred\n"));
}

TEST(Session, StatisticsCountTraffic)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());

    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("12345\n")));
    fixture.session.send(QByteArrayLiteral("ab"));

    ASSERT_TRUE(waitFor([&] { return fixture.session.statistics().bytesSent == 2; }));
    EXPECT_EQ(fixture.session.statistics().bytesReceived, 6);
}

TEST(Session, FixedLengthPacketizerSplitsIncomingData)
{
    Fixture fixture;
    fixture.session.setPacketizerMode(Packetizer::Mode::FixedLength);
    fixture.session.setPacketizerFixedLength(2);
    ASSERT_TRUE(fixture.openDevice());

    const qint64 before = fixture.session.buffer()->nextLineNumber();
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("abcdef")));

    // Три пакета по два байта — три строки, хотя переводов строк в данных не было.
    EXPECT_TRUE(waitFor([&] {
        return fixture.session.buffer()->nextLineNumber() - before == 3;
    }));
}

TEST(Session, DelimiterPacketizerSplitsIncomingData)
{
    Fixture fixture;
    fixture.session.setPacketizerMode(Packetizer::Mode::Delimiter);
    fixture.session.setPacketizerDelimiter(QByteArrayLiteral(";"));
    ASSERT_TRUE(fixture.openDevice());

    const qint64 before = fixture.session.buffer()->nextLineNumber();
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("one;two;")));

    EXPECT_TRUE(waitFor([&] {
        return fixture.session.buffer()->nextLineNumber() - before == 2;
    }));
}

TEST(Session, CloseStopsTheChannel)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());

    fixture.session.close();

    EXPECT_EQ(fixture.session.state(), ChannelState::Closed);
    EXPECT_FALSE(fixture.session.isActive());
}

TEST(Session, ReopenAfterCloseWorks)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());
    fixture.session.close();

    fixture.session.open();

    EXPECT_TRUE(waitFor([&] { return fixture.session.state() == ChannelState::Open; }));
}

TEST(Session, LostDeviceBecomesUnavailable)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());

    fixture.plugin.devices.clear();
    fixture.registry.refresh();

    // Именно Unavailable, а не Closed: пользователь порт не закрывал.
    EXPECT_TRUE(waitFor([&] {
        return fixture.session.state() == ChannelState::Unavailable;
    }));
}

TEST(Session, ReturningDeviceIsReopenedAutomatically)
{
    Fixture fixture;
    const auto device = FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                        QStringLiteral("dev-a"));
    ASSERT_TRUE(fixture.openDevice());

    fixture.plugin.devices.clear();
    fixture.registry.refresh();
    ASSERT_TRUE(waitFor([&] {
        return fixture.session.state() == ChannelState::Unavailable;
    }));

    fixture.plugin.devices = {device};
    fixture.registry.refresh();

    // Ради этого перехода и различаются Closed и Unavailable.
    EXPECT_TRUE(waitFor([&] { return fixture.session.state() == ChannelState::Open; }));
}

TEST(Session, ExplicitCloseDisablesAutomaticReopen)
{
    Fixture fixture;
    const auto device = FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                        QStringLiteral("dev-a"));
    ASSERT_TRUE(fixture.openDevice());

    fixture.session.close();

    fixture.plugin.devices.clear();
    fixture.registry.refresh();
    fixture.plugin.devices = {device};
    fixture.registry.refresh();

    // Открывать порт, который пользователь только что закрыл, значило бы спорить с ним.
    waitFor([] { return false; }, 200);
    EXPECT_EQ(fixture.session.state(), ChannelState::Closed);
}

TEST(Session, OpeningAbsentDeviceWaitsForIt)
{
    Fixture fixture;
    const auto device = FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                        QStringLiteral("dev-a"));

    fixture.plugin.devices.clear();
    fixture.registry.refresh();

    fixture.session.setInterfaceId(kDeviceId);
    fixture.session.open();

    EXPECT_EQ(fixture.session.state(), ChannelState::Unavailable);

    // Намерение запомнено: устройство появится — порт откроется сам.
    fixture.plugin.devices = {device};
    fixture.registry.refresh();

    EXPECT_TRUE(waitFor([&] { return fixture.session.state() == ChannelState::Open; }));
}

TEST(Session, ChangingInterfaceClosesTheOldOneWithoutClearingOutput)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());
    ASSERT_TRUE(fixture.receive(QByteArrayLiteral("before-switch\n")));

    fixture.session.setInterfaceId(QStringLiteral("fake:other"));

    EXPECT_EQ(fixture.session.state(), ChannelState::Closed);
    EXPECT_EQ(fixture.session.interfaceId(), QStringLiteral("fake:other"));
    ASSERT_EQ(fixture.session.buffer()->lineCount(), 3);
    EXPECT_EQ(fixture.session.buffer()->line(1)->text, QStringLiteral("before-switch"));
    EXPECT_EQ(fixture.session.buffer()->line(2)->direction, DataDirection::System);
}

TEST(Session, ControlLinesAreReported)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.openDevice());

    EXPECT_TRUE(waitFor([&] {
        return fixture.session.controlLines().value(QStringLiteral("CTS")).toBool();
    }));
}

TEST(Session, UnknownInterfaceReportsError)
{
    Fixture fixture;

    QStringList errors;
    QObject::connect(&fixture.session, &Session::errorOccurred,
                     [&](const QString &message) { errors.append(message); });

    fixture.session.setInterfaceId(QStringLiteral("fake:missing"));
    fixture.session.open();

    // Устройства нет в реестре — сессия сообщает об этом, а не молчит.
    EXPECT_FALSE(errors.isEmpty() && fixture.session.state() == ChannelState::Open);
}

TEST(Session, DestroyingOpenSessionStopsCleanly)
{
    // Поток ввода-вывода должен останавливаться в деструкторе: иначе он пережил бы
    // объекты, на которые ссылается.
    auto fixture = std::make_unique<Fixture>();
    ASSERT_TRUE(fixture->openDevice());

    QPointer<FakeChannel> channel = fixture->plugin.lastChannel;
    ASSERT_FALSE(channel.isNull());

    fixture.reset();

    EXPECT_TRUE(waitFor([&] { return channel.isNull(); }));
}
