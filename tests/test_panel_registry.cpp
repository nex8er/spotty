/**
 * \file test_panel_registry.cpp
 * \brief Тесты spotty::PanelPluginRegistry и второй фазы загрузки плагинов.
 */
#include "support/FakeInterfacePlugin.h"
#include "support/FakePanelPlugin.h"

#include <PanelPluginRegistry.h>
#include <PluginManager.h>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::FakeInterfacePlugin;
using spotty::test::FakePanelPlugin;
using spotty::test::makePanel;

TEST(PanelRegistry, AcceptsPluginWithItsPanels)
{
    PanelPluginRegistry registry;
    FakePanelPlugin plugin;
    plugin.declaredPanels = {makePanel(QStringLiteral("a")), makePanel(QStringLiteral("b"))};

    EXPECT_TRUE(registry.addBuiltin(&plugin));
    EXPECT_EQ(registry.plugins().size(), 1);
    EXPECT_EQ(registry.panels().size(), 2);
    EXPECT_TRUE(registry.failures().isEmpty());
}

TEST(PanelRegistry, RejectsMismatchedApiVersion)
{
    PanelPluginRegistry registry;
    FakePanelPlugin plugin;
    plugin.apiVersion = SPOTTY_UI_API_VERSION + 1;
    plugin.declaredPanels = {makePanel(QStringLiteral("a"))};

    EXPECT_FALSE(registry.addBuiltin(&plugin));
    EXPECT_TRUE(registry.plugins().isEmpty());
    EXPECT_EQ(registry.failures().size(), 1);
}

TEST(PanelRegistry, RejectsEmptyPluginId)
{
    PanelPluginRegistry registry;
    FakePanelPlugin plugin(QString{});

    EXPECT_FALSE(registry.addBuiltin(&plugin));
    EXPECT_EQ(registry.failures().size(), 1);
}

TEST(PanelRegistry, RejectsDuplicatePluginId)
{
    PanelPluginRegistry registry;
    FakePanelPlugin first(QStringLiteral("same"));
    FakePanelPlugin second(QStringLiteral("same"));
    first.declaredPanels = {makePanel(QStringLiteral("a"))};
    second.declaredPanels = {makePanel(QStringLiteral("b"))};

    EXPECT_TRUE(registry.addBuiltin(&first));
    EXPECT_FALSE(registry.addBuiltin(&second));
    EXPECT_EQ(registry.plugins().size(), 1);
}

TEST(PanelRegistry, RejectsDuplicatePanelId)
{
    PanelPluginRegistry registry;
    FakePanelPlugin first(QStringLiteral("one"));
    FakePanelPlugin second(QStringLiteral("two"));
    first.declaredPanels = {makePanel(QStringLiteral("shared"))};
    second.declaredPanels = {makePanel(QStringLiteral("shared"))};

    EXPECT_TRUE(registry.addBuiltin(&first));
    // Идентификатор панели уникален глобально: он попадает в настройки и служит адресом
    // для activatePanel().
    EXPECT_FALSE(registry.addBuiltin(&second));
}

TEST(PanelRegistry, RejectedPluginRegistersNoneOfItsPanels)
{
    PanelPluginRegistry registry;
    FakePanelPlugin first(QStringLiteral("one"));
    FakePanelPlugin second(QStringLiteral("two"));
    first.declaredPanels = {makePanel(QStringLiteral("shared"))};
    // Первая панель уникальна, вторая — нет. Плагин должен быть отвергнут целиком:
    // половина зарегистрированных панелей оставила бы его в состоянии, которого он не
    // предусматривал.
    second.declaredPanels = {makePanel(QStringLiteral("fresh")),
                             makePanel(QStringLiteral("shared"))};

    EXPECT_TRUE(registry.addBuiltin(&first));
    EXPECT_FALSE(registry.addBuiltin(&second));
    EXPECT_EQ(registry.panels().size(), 1);
    EXPECT_EQ(registry.panel(QStringLiteral("fresh")), nullptr);
}

TEST(PanelRegistry, PanelsAreOrderedByOrderThenId)
{
    PanelPluginRegistry registry;
    FakePanelPlugin plugin;
    plugin.declaredPanels = {
        makePanel(QStringLiteral("z"), 100),
        makePanel(QStringLiteral("a"), 300),
        makePanel(QStringLiteral("b"), 100),
    };

    ASSERT_TRUE(registry.addBuiltin(&plugin));
    ASSERT_EQ(registry.panels().size(), 3);
    // Порядок задают число и идентификатор, а не очерёдность объявления: та зависела бы
    // от обхода каталогов и отличалась бы от машины к машине.
    EXPECT_EQ(registry.panels().at(0).descriptor.id, QStringLiteral("b"));
    EXPECT_EQ(registry.panels().at(1).descriptor.id, QStringLiteral("z"));
    EXPECT_EQ(registry.panels().at(2).descriptor.id, QStringLiteral("a"));
}

TEST(PanelRegistry, LookupsFindWhatWasRegistered)
{
    PanelPluginRegistry registry;
    FakePanelPlugin plugin(QStringLiteral("kit"));
    plugin.declaredPanels = {makePanel(QStringLiteral("kit.main"))};

    ASSERT_TRUE(registry.addBuiltin(&plugin));
    EXPECT_EQ(registry.plugin(QStringLiteral("kit")), &plugin);
    EXPECT_EQ(registry.plugin(QStringLiteral("nope")), nullptr);
    ASSERT_NE(registry.panel(QStringLiteral("kit.main")), nullptr);
    EXPECT_EQ(registry.panel(QStringLiteral("kit.main"))->plugin, &plugin);
    EXPECT_EQ(registry.panel(QStringLiteral("missing")), nullptr);
}

TEST(PanelRegistry, PluginWithoutPanelsIsStillAccepted)
{
    PanelPluginRegistry registry;
    FakePanelPlugin plugin;

    // Звено цепочки преобразования может не показывать ничего — это законный плагин.
    EXPECT_TRUE(registry.addBuiltin(&plugin));
    EXPECT_EQ(registry.plugins().size(), 1);
    EXPECT_TRUE(registry.panels().isEmpty());
}

// --- Выключение плагинов ------------------------------------------------------------

TEST(PanelRegistryDisable, DisabledPluginIsSkippedWithoutFailure)
{
    PanelPluginRegistry registry;
    registry.setDisabledPlugins({QStringLiteral("fake")});

    FakePanelPlugin plugin;
    plugin.declaredPanels = {makePanel(QStringLiteral("a"))};

    EXPECT_FALSE(registry.addBuiltin(&plugin));
    EXPECT_TRUE(registry.plugins().isEmpty());

    // Панели выключенного не существует: её не строили и адреса для activatePanel() у неё
    // нет. Показывать её в списке значило бы обещать то, чего в программе нет.
    EXPECT_TRUE(registry.panels().isEmpty());

    // И это не отказ: в failures() ищут причину, по которой плагин пропал сам собой.
    EXPECT_TRUE(registry.failures().isEmpty());
}

TEST(PanelRegistryDisable, DisabledPluginIsOfferedForReenabling)
{
    PanelPluginRegistry registry;
    registry.setDisabledPlugins({QStringLiteral("fake")});

    FakePanelPlugin plugin;
    registry.addBuiltin(&plugin);

    ASSERT_EQ(registry.disabledPanelPlugins().size(), 1);
    EXPECT_EQ(registry.disabledPanelPlugins().first(), &plugin);
}

TEST(PanelRegistryDisable, DisabledIdIsFreeForOthers)
{
    PanelPluginRegistry registry;
    registry.setDisabledPlugins({QStringLiteral("off")});

    FakePanelPlugin disabled(QStringLiteral("off"));
    FakePanelPlugin enabled(QStringLiteral("on"));
    enabled.declaredPanels = {makePanel(QStringLiteral("a"))};

    EXPECT_FALSE(registry.addBuiltin(&disabled));
    EXPECT_TRUE(registry.addBuiltin(&enabled));
    EXPECT_EQ(registry.plugins().size(), 1);
    EXPECT_EQ(registry.panels().size(), 1);
}

TEST(PanelRegistryDisable, DisabledPluginIsHonouredOnTheScanPath)
{
    PluginManager manager;
    FakePanelPlugin plugin;
    plugin.declaredPanels = {makePanel(QStringLiteral("a"))};

    // Менеджер выносит приговор своей роли сразу (панель ему чужая) — но экземпляр всё
    // равно попадает в instances(), откуда его и берёт реестр. Это тот же путь, которым
    // идёт настоящая загрузка из каталога.
    manager.addPlugin(&plugin, QStringLiteral("<test>"));

    PanelPluginRegistry panels(&manager);
    panels.setDisabledPlugins({QStringLiteral("fake")});
    panels.load();

    ASSERT_EQ(panels.disabledPanelPlugins().size(), 1);
    EXPECT_TRUE(panels.plugins().isEmpty());
    EXPECT_TRUE(panels.panels().isEmpty());
    EXPECT_TRUE(panels.failures().isEmpty());
}

// --- Две фазы загрузки --------------------------------------------------------------

TEST(TwoPhaseLoading, InterfacePluginIsNotReportedAsUnrecognized)
{
    PluginManager manager;
    FakeInterfacePlugin plugin;

    ASSERT_TRUE(manager.addPlugin(&plugin));

    PanelPluginRegistry panels(&manager);
    panels.load();
    manager.finishLoading();

    // Транспорт свою роль нашёл — панельный реестр его не тронул и жаловаться не на что.
    EXPECT_TRUE(manager.failures().isEmpty());
}

TEST(TwoPhaseLoading, UnknownObjectIsReportedOnceAfterFinish)
{
    PluginManager manager;
    QObject stranger;

    // addPlugin() судит сразу: вызывающий передал конкретный экземпляр и ждёт ответа.
    EXPECT_FALSE(manager.addPlugin(&stranger));
    EXPECT_EQ(manager.failures().size(), 1);

    PanelPluginRegistry panels(&manager);
    panels.load();
    manager.finishLoading();

    // И ровно одна запись, а не две: приговор уже вынесен, повторять его незачем.
    EXPECT_EQ(manager.failures().size(), 1);
}

TEST(TwoPhaseLoading, FinishLoadingIsIdempotent)
{
    PluginManager manager;
    QObject stranger;
    manager.addPlugin(&stranger);

    manager.finishLoading();
    const int after = manager.failures().size();
    manager.finishLoading();

    EXPECT_EQ(manager.failures().size(), after);
}
