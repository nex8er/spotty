/**
 * \file test_settings_migration.cpp
 * \brief Тесты переноса настроек между версиями.
 */
#include <settings/SettingsMigration.h>
#include <settings/SettingsStore.h>

#include "support/TestSupport.h"

#include <gtest/gtest.h>

using namespace spotty;

namespace {

/// \brief Настройки во временном файле: перенос обязан работать через настоящий store.
class Migration : public ::testing::Test
{
protected:
    test::TempDir dir;

    SettingsStore makeStore() { return SettingsStore(dir.filePath("settings.json")); }
};

} // namespace

TEST_F(Migration, LegacyPluginKeysStillMove)
{
    // Первый в истории тест на уже существовавший перенос: он жил статической функцией в
    // main.cpp и не был достижим ни для одного набора.
    SettingsStore store = makeStore();
    store.setValue(QStringLiteral("macros/preset"), QStringLiteral("board"));
    store.setValue(QStringLiteral("logging/includeTx"), true);

    EXPECT_TRUE(SettingsMigration::apply(store));

    EXPECT_EQ(store.value(QStringLiteral("plugins/macros/preset")).toString(),
              QStringLiteral("board"));
    EXPECT_TRUE(store.value(QStringLiteral("plugins/logging/includeTx")).toBool());
    EXPECT_FALSE(store.contains(QStringLiteral("macros/preset")));
}

TEST_F(Migration, LegacyKeyDoesNotOverwriteNewerValue)
{
    // Повторный запуск не должен затирать то, что человек изменил уже в новой версии.
    SettingsStore store = makeStore();
    store.setValue(QStringLiteral("macros/preset"), QStringLiteral("старое"));
    store.setValue(QStringLiteral("plugins/macros/preset"), QStringLiteral("новое"));

    SettingsMigration::apply(store);

    EXPECT_EQ(store.value(QStringLiteral("plugins/macros/preset")).toString(),
              QStringLiteral("новое"));
}

TEST_F(Migration, CsvchartSubtreeMovesToPlotter)
{
    SettingsStore store = makeStore();
    store.setValue(QStringLiteral("plugins/csvchart/separator"), QStringLiteral(";"));
    store.setValue(QStringLiteral("plugins/csvchart/somethingNew"), 42);

    EXPECT_TRUE(SettingsMigration::apply(store));

    EXPECT_EQ(store.value(QStringLiteral("plugins/plotter/separator")).toString(),
              QStringLiteral(";"));
    // Поддерево переезжает целиком, поэтому ключ, заведённый между версиями, тоже уцелел.
    EXPECT_EQ(store.value(QStringLiteral("plugins/plotter/somethingNew")).toInt(), 42);
    EXPECT_FALSE(store.contains(QStringLiteral("plugins/csvchart/separator")));
}

TEST_F(Migration, ExistingPlotterValuesWin)
{
    SettingsStore store = makeStore();
    store.setValue(QStringLiteral("plugins/csvchart/separator"), QStringLiteral(";"));
    store.setValue(QStringLiteral("plugins/plotter/separator"), QStringLiteral("\t"));

    SettingsMigration::apply(store);

    EXPECT_EQ(store.value(QStringLiteral("plugins/plotter/separator")).toString(),
              QStringLiteral("\t"));
}

TEST_F(Migration, PointsBecomeCapacity)
{
    // «Window: N points» задавало размер буфера; смысл сохраняется под новым именем.
    SettingsStore store = makeStore();
    store.setValue(QStringLiteral("plugins/csvchart/points"), 2000);

    SettingsMigration::apply(store);

    EXPECT_EQ(store.value(QStringLiteral("plugins/plotter/capacity")).toInt(), 2000);
    EXPECT_FALSE(store.contains(QStringLiteral("plugins/plotter/points")));
}

TEST_F(Migration, PanelIdIsRewrittenOnlyWhenItWasCsvchart)
{
    SettingsStore store = makeStore();
    store.setValue(QStringLiteral("window/panelId"), QStringLiteral("csvchart"));
    SettingsMigration::apply(store);
    EXPECT_EQ(store.value(QStringLiteral("window/panelId")).toString(),
              QStringLiteral("plotter"));
}

TEST_F(Migration, AnotherPluginsPanelIsLeftAlone)
{
    // Сверка по значению, а не по наличию ключа: у выбравшего панель поиска её не отберут.
    SettingsStore store = makeStore();
    store.setValue(QStringLiteral("window/panelId"), QStringLiteral("search"));
    store.setValue(QStringLiteral("window/viewStrip"), QStringLiteral("other.plot"));

    SettingsMigration::apply(store);

    EXPECT_EQ(store.value(QStringLiteral("window/panelId")).toString(),
              QStringLiteral("search"));
    EXPECT_EQ(store.value(QStringLiteral("window/viewStrip")).toString(),
              QStringLiteral("other.plot"));
}

TEST_F(Migration, ViewStripIsRewritten)
{
    SettingsStore store = makeStore();
    store.setValue(QStringLiteral("window/viewStrip"), QStringLiteral("csvchart.plot"));

    SettingsMigration::apply(store);

    EXPECT_EQ(store.value(QStringLiteral("window/viewStrip")).toString(),
              QStringLiteral("plotter.plot"));
}

TEST_F(Migration, MigrationIsIdempotent)
{
    // Перенос отрабатывает на каждом запуске, пока его не удалят из следующей версии.
    SettingsStore store = makeStore();
    store.setValue(QStringLiteral("plugins/csvchart/separator"), QStringLiteral(";"));
    store.setValue(QStringLiteral("window/panelId"), QStringLiteral("csvchart"));

    EXPECT_TRUE(SettingsMigration::apply(store));
    EXPECT_FALSE(SettingsMigration::apply(store));

    EXPECT_EQ(store.value(QStringLiteral("plugins/plotter/separator")).toString(),
              QStringLiteral(";"));
    EXPECT_EQ(store.value(QStringLiteral("window/panelId")).toString(),
              QStringLiteral("plotter"));
}

TEST_F(Migration, NothingToDoLeavesSettingsUntouched)
{
    SettingsStore store = makeStore();
    store.setValue(QStringLiteral("plugins/plotter/separator"), QStringLiteral(","));

    EXPECT_FALSE(SettingsMigration::apply(store));
}
