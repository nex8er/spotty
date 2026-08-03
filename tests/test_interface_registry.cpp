/**
 * \file test_interface_registry.cpp
 * \brief Тесты spotty::InterfaceRegistry с поддельным плагином.
 */
#include "support/FakeInterfacePlugin.h"
#include "support/TestSupport.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <settings/SettingsStore.h>

#include <QStringList>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::FakeInterfacePlugin;
using spotty::test::TempDir;
using spotty::test::waitFor;

namespace {

/**
 * \struct Fixture
 * \brief Обвязка «менеджер + хранилище + реестр» для одного теста.
 *
 * Плагин регистрируется тем же путём, что и вкомпилированный, поэтому проверки версии
 * API и повторяющегося идентификатора работают как в бою.
 */
struct Fixture
{
    TempDir dir;
    PluginManager plugins;
    FakeInterfacePlugin plugin;
    SettingsStore store;
    InterfaceRegistry registry;

    Fixture()
        : store(dir.filePath(QStringLiteral("interfaces.json")))
        , registry(&plugins, &store)
    {
        EXPECT_TRUE(plugins.addPlugin(&plugin));
        store.load();
    }
};

} // namespace

TEST(InterfaceRegistry, DiscoversDevicesFromPlugin)
{
    Fixture fixture;
    fixture.plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                              QStringLiteral("dev-a"))};

    fixture.registry.refresh();

    const QList<InterfaceEntry> entries = fixture.registry.entries();
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries.first().descriptor.id, QStringLiteral("fake:a"));
    EXPECT_TRUE(entries.first().present);
}

TEST(InterfaceRegistry, PluginIdIsFilledByRegistry)
{
    Fixture fixture;
    // Плагин намеренно оставляет поле пустым: так исключается целый класс ошибок, когда
    // он указывает чужой идентификатор.
    fixture.plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                              QStringLiteral("dev-a"))};
    ASSERT_TRUE(fixture.plugin.devices.first().pluginId.isEmpty());

    fixture.registry.refresh();

    ASSERT_NE(fixture.registry.entry(QStringLiteral("fake:a")), nullptr);
    EXPECT_EQ(fixture.registry.entry(QStringLiteral("fake:a"))->descriptor.pluginId,
              QStringLiteral("fake"));
}

TEST(InterfaceRegistry, DescriptorWithoutIdIsRejected)
{
    Fixture fixture;
    InterfaceDescriptor broken;
    broken.systemName = QStringLiteral("no-id");
    fixture.plugin.devices = {broken};

    fixture.registry.refresh();

    EXPECT_TRUE(fixture.registry.entries().isEmpty());
}

TEST(InterfaceRegistry, AppearanceIsReported)
{
    Fixture fixture;

    QStringList appeared;
    QObject::connect(&fixture.registry, &InterfaceRegistry::interfaceAppeared,
                     [&](const QString &id) { appeared.append(id); });

    fixture.plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                              QStringLiteral("dev-a"))};
    fixture.registry.refresh();

    EXPECT_EQ(appeared, QStringList({QStringLiteral("fake:a")}));
}

TEST(InterfaceRegistry, DisappearanceIsReportedAndEntryKept)
{
    Fixture fixture;
    fixture.plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                              QStringLiteral("dev-a"))};
    fixture.registry.refresh();

    QStringList disappeared;
    QObject::connect(&fixture.registry, &InterfaceRegistry::interfaceDisappeared,
                     [&](const QString &id) { disappeared.append(id); });

    fixture.plugin.devices.clear();
    fixture.registry.refresh();

    EXPECT_EQ(disappeared, QStringList({QStringLiteral("fake:a")}));

    // Запись остаётся: настроенный порт, который сейчас не подключён, должен быть на
    // виду, а не исчезать бесследно.
    ASSERT_NE(fixture.registry.entry(QStringLiteral("fake:a")), nullptr);
    EXPECT_FALSE(fixture.registry.entry(QStringLiteral("fake:a"))->present);
}

TEST(InterfaceRegistry, RepeatedRefreshDoesNotRepeatSignals)
{
    Fixture fixture;
    fixture.plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                              QStringLiteral("dev-a"))};
    fixture.registry.refresh();

    int appearances = 0;
    QObject::connect(&fixture.registry, &InterfaceRegistry::interfaceAppeared,
                     [&] { ++appearances; });

    // Опрос идёт раз в секунду; сигнал на каждом обходе сделал бы его бесполезным.
    fixture.registry.refresh();
    fixture.registry.refresh();

    EXPECT_EQ(appearances, 0);
}

TEST(InterfaceRegistry, ReappearanceIsReportedAgain)
{
    Fixture fixture;
    const auto device = FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                        QStringLiteral("dev-a"));

    fixture.plugin.devices = {device};
    fixture.registry.refresh();
    fixture.plugin.devices.clear();
    fixture.registry.refresh();

    int appearances = 0;
    QObject::connect(&fixture.registry, &InterfaceRegistry::interfaceAppeared,
                     [&] { ++appearances; });

    fixture.plugin.devices = {device};
    fixture.registry.refresh();

    // Именно по этому сигналу сессия открывает порт заново.
    EXPECT_EQ(appearances, 1);
}

TEST(InterfaceRegistry, DiscoveredAtIsSetOnAppearanceAndClearedOnLoss)
{
    Fixture fixture;
    const auto device = FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                        QStringLiteral("dev-a"));

    fixture.plugin.devices = {device};
    fixture.registry.refresh();
    EXPECT_TRUE(fixture.registry.entry(QStringLiteral("fake:a"))->discoveredAt.isValid());

    fixture.plugin.devices.clear();
    fixture.registry.refresh();
    EXPECT_FALSE(fixture.registry.entry(QStringLiteral("fake:a"))->discoveredAt.isValid());
}

TEST(InterfaceRegistry, AliasIsStoredAndUsedForDisplay)
{
    Fixture fixture;
    fixture.plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                              QStringLiteral("dev-a"))};
    fixture.registry.refresh();

    EXPECT_EQ(fixture.registry.entry(QStringLiteral("fake:a"))->displayName(),
              QStringLiteral("dev-a"));

    fixture.registry.setAlias(QStringLiteral("fake:a"), QStringLiteral("Board 1"));

    EXPECT_EQ(fixture.registry.entry(QStringLiteral("fake:a"))->displayName(),
              QStringLiteral("Board 1"));
}

TEST(InterfaceRegistry, AliasAndSettingsSurviveReload)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("interfaces.json"));
    const auto device = FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                        QStringLiteral("dev-a"));

    {
        PluginManager plugins;
        FakeInterfacePlugin plugin;
        plugin.devices = {device};
        ASSERT_TRUE(plugins.addPlugin(&plugin));

        SettingsStore store(path);
        store.load();
        InterfaceRegistry registry(&plugins, &store);
        registry.refresh();

        registry.setAlias(QStringLiteral("fake:a"), QStringLiteral("Board 1"));
        registry.setSettingsFor(QStringLiteral("fake:a"),
                                {{QStringLiteral("speed"), 57600},
                                 {QStringLiteral("mode"), QStringLiteral("8E1")}});
        ASSERT_TRUE(store.save());
    }

    PluginManager plugins;
    FakeInterfacePlugin plugin;
    plugin.devices = {device};
    ASSERT_TRUE(plugins.addPlugin(&plugin));

    SettingsStore store(path);
    ASSERT_TRUE(store.load());
    InterfaceRegistry registry(&plugins, &store);
    registry.start();

    ASSERT_NE(registry.entry(QStringLiteral("fake:a")), nullptr);
    EXPECT_EQ(registry.entry(QStringLiteral("fake:a"))->alias, QStringLiteral("Board 1"));
    EXPECT_EQ(registry.settingsFor(QStringLiteral("fake:a"))
                  .value(QStringLiteral("speed")).toInt(),
              57600);
}

TEST(InterfaceRegistry, SettingsAreNormalisedAgainstSchema)
{
    Fixture fixture;
    fixture.plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                              QStringLiteral("dev-a"))};
    fixture.registry.refresh();

    // Записываем только один ключ и лишний, которого в схеме нет.
    fixture.registry.setSettingsFor(QStringLiteral("fake:a"),
                                    {{QStringLiteral("speed"), 19200},
                                     {QStringLiteral("removedInNewVersion"), 1}});

    const QVariantMap settings = fixture.registry.settingsFor(QStringLiteral("fake:a"));

    // Нормализация при чтении — то, что позволяет конфигурации пережить появление или
    // исчезновение параметра между версиями плагина.
    EXPECT_EQ(settings.value(QStringLiteral("speed")).toInt(), 19200);
    EXPECT_EQ(settings.value(QStringLiteral("mode")).toString(), QStringLiteral("8N1"));
    EXPECT_FALSE(settings.contains(QStringLiteral("removedInNewVersion")));
}

TEST(InterfaceRegistry, SettingsForUnknownIdIsEmpty)
{
    Fixture fixture;

    EXPECT_TRUE(fixture.registry.settingsFor(QStringLiteral("fake:nothing")).isEmpty());
    EXPECT_EQ(fixture.registry.entry(QStringLiteral("fake:nothing")), nullptr);
}

TEST(InterfaceRegistry, PresentDevicesAreListedFirst)
{
    Fixture fixture;
    const auto present = FakeInterfacePlugin::makeDevice(QStringLiteral("z"),
                                                          QStringLiteral("zzz"));
    const auto absent = FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                         QStringLiteral("aaa"));

    fixture.plugin.devices = {present, absent};
    fixture.registry.refresh();

    fixture.plugin.devices = {present};
    fixture.registry.refresh();

    const QList<InterfaceEntry> entries = fixture.registry.entries();
    ASSERT_EQ(entries.size(), 2);
    // Присутствующее устройство идёт первым, несмотря на то что по алфавиту оно последнее.
    EXPECT_TRUE(entries.first().present);
    EXPECT_EQ(entries.first().descriptor.id, QStringLiteral("fake:z"));
}

TEST(InterfaceRegistry, ChangedSignalOnlyWhenSomethingChanged)
{
    Fixture fixture;
    fixture.plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                              QStringLiteral("dev-a"))};
    fixture.registry.refresh();

    int changes = 0;
    QObject::connect(&fixture.registry, &InterfaceRegistry::changed, [&] { ++changes; });

    fixture.registry.refresh();
    EXPECT_EQ(changes, 0);

    fixture.plugin.devices.clear();
    fixture.registry.refresh();
    EXPECT_EQ(changes, 1);
}
