/**
 * \file test_plugin_manager.cpp
 * \brief Тесты spotty::PluginManager: выключение плагинов и его отличие от отказа.
 */
#include "support/FakeInterfacePlugin.h"

#include <PluginManager.h>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::FakeInterfacePlugin;

TEST(PluginManagerDisable, DisabledPluginIsNotRegistered)
{
    PluginManager manager;
    manager.setDisabledPlugins({QStringLiteral("fake")});

    FakeInterfacePlugin plugin;
    EXPECT_FALSE(manager.addPlugin(&plugin));
    EXPECT_TRUE(manager.plugins().isEmpty());
    EXPECT_EQ(manager.plugin(QStringLiteral("fake")), nullptr);
}

TEST(PluginManagerDisable, DisabledPluginIsNotAFailure)
{
    PluginManager manager;
    manager.setDisabledPlugins({QStringLiteral("fake")});

    FakeInterfacePlugin plugin;
    manager.addPlugin(&plugin);

    // В failures() ищут причину, по которой плагин пропал сам собой. Выключенный вручную
    // там только мешал бы — и мешал бы ровно тому, кто ищет настоящую поломку.
    EXPECT_TRUE(manager.failures().isEmpty());
}

TEST(PluginManagerDisable, DisabledPluginIsOfferedForReenabling)
{
    PluginManager manager;
    manager.setDisabledPlugins({QStringLiteral("fake")});

    FakeInterfacePlugin plugin;
    manager.addPlugin(&plugin);

    // Диалогу настроек нужен сам объект: без него флажок обратного включения нечем
    // подписать, и выключенный плагин исчез бы из программы навсегда.
    ASSERT_EQ(manager.disabledInterfacePlugins().size(), 1);
    EXPECT_EQ(manager.disabledInterfacePlugins().first(), &plugin);
}

TEST(PluginManagerDisable, DisabledPluginIsNotReportedAsUnrecognized)
{
    PluginManager manager;
    manager.setDisabledPlugins({QStringLiteral("fake")});

    FakeInterfacePlugin plugin;
    manager.addPlugin(&plugin);
    manager.finishLoading();

    // Роль плагина разобрана — просто он выключен. Без пометки «распознан» finishLoading()
    // объявил бы его посторонней библиотекой, и отчёт врал бы о причине.
    EXPECT_TRUE(manager.failures().isEmpty());
}

TEST(PluginManagerDisable, OtherPluginsAreUnaffected)
{
    PluginManager manager;
    manager.setDisabledPlugins({QStringLiteral("off")});

    FakeInterfacePlugin disabled(QStringLiteral("off"));
    FakeInterfacePlugin enabled(QStringLiteral("on"));

    EXPECT_FALSE(manager.addPlugin(&disabled));
    EXPECT_TRUE(manager.addPlugin(&enabled));

    ASSERT_EQ(manager.plugins().size(), 1);
    EXPECT_EQ(manager.plugins().first()->pluginId(), QStringLiteral("on"));
}

TEST(PluginManagerDisable, DisabledIdDoesNotOccupyTheName)
{
    PluginManager manager;
    manager.setDisabledPlugins({QStringLiteral("fake")});

    // Две копии одного плагина в разных каталогах — обычное дело при обновлении. Обе
    // выключены, и в список для диалога плагин обязан попасть один раз: две строки с одним
    // и тем же флажком выглядели бы поломкой.
    FakeInterfacePlugin first;
    FakeInterfacePlugin second;
    manager.addPlugin(&first, QStringLiteral("<dir-a>"));
    manager.addPlugin(&second, QStringLiteral("<dir-b>"));

    EXPECT_EQ(manager.disabledInterfacePlugins().size(), 1);
    EXPECT_TRUE(manager.failures().isEmpty());
}

TEST(PluginManagerDisable, MismatchedApiVersionOutranksDisabling)
{
    PluginManager manager;
    manager.setDisabledPlugins({QStringLiteral("fake")});

    FakeInterfacePlugin plugin;
    plugin.apiVersionOverride = SPOTTY_API_VERSION + 1;

    EXPECT_FALSE(manager.addPlugin(&plugin));

    // Проверка версии идёт первой не из вредности: pluginId() у плагина, собранного против
    // чужой версии, вызывать нельзя — а без идентификатора сверять список не с чем.
    EXPECT_EQ(manager.failures().size(), 1);
    EXPECT_TRUE(manager.disabledInterfacePlugins().isEmpty());
}
