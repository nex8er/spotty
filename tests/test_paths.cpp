/**
 * \file test_paths.cpp
 * \brief Тесты spotty::Paths и spotty::PluginManager.
 */
#include "support/FakeInterfacePlugin.h"
#include "support/TestSupport.h"

#include <PluginManager.h>
#include <settings/Paths.h>

#include <QDir>

#include <QStringList>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::FakeInterfacePlugin;
using spotty::test::TempDir;

namespace {

/// \brief Плагин с несовпадающей версией API.
class OutdatedPlugin : public FakeInterfacePlugin
{
public:
    int apiVersion() const override { return SPOTTY_API_VERSION + 1; }
};

/// \brief Плагин с пустым идентификатором.
class NamelessPlugin : public FakeInterfacePlugin
{
public:
    QString pluginId() const override { return {}; }
};

} // namespace

TEST(Paths, InitializeProducesUsablePaths)
{
    Paths::initialize();

    EXPECT_FALSE(Paths::configDir().isEmpty());
    EXPECT_TRUE(Paths::settingsFile().endsWith(QStringLiteral("settings.json")));
    EXPECT_TRUE(Paths::interfacesFile().endsWith(QStringLiteral("interfaces.json")));
    EXPECT_TRUE(Paths::historyFile().endsWith(QStringLiteral("history.txt")));
    EXPECT_TRUE(Paths::macrosDir().endsWith(QStringLiteral("macros")));
    EXPECT_FALSE(Paths::defaultLogDir().isEmpty());
}

TEST(Paths, AllFilesLiveInsideConfigDirectory)
{
    Paths::initialize();
    const QString config = Paths::configDir();

    EXPECT_TRUE(Paths::settingsFile().startsWith(config));
    EXPECT_TRUE(Paths::interfacesFile().startsWith(config));
    EXPECT_TRUE(Paths::historyFile().startsWith(config));
    EXPECT_TRUE(Paths::macrosDir().startsWith(config));
}

TEST(Paths, EnsureDirCreatesNestedDirectories)
{
    TempDir dir;
    const QString nested = QDir(dir.path()).filePath(QStringLiteral("a/b/c"));

    EXPECT_TRUE(Paths::ensureDir(nested));
    EXPECT_TRUE(QDir(nested).exists());

    // Повторный вызов на существующем каталоге тоже успешен.
    EXPECT_TRUE(Paths::ensureDir(nested));
}

TEST(Paths, PluginDirsAreUniqueAndExisting)
{
    Paths::initialize();
    const QStringList dirs = Paths::pluginDirs();

    QStringList seen;
    for (const QString &dir : dirs) {
        EXPECT_TRUE(QDir(dir).exists()) << dir.toStdString();
        EXPECT_FALSE(seen.contains(dir)) << "duplicate: " << dir.toStdString();
        seen.append(dir);
    }
}

TEST(PluginManager, AcceptsValidPlugin)
{
    PluginManager manager;
    FakeInterfacePlugin plugin;

    EXPECT_TRUE(manager.addPlugin(&plugin));
    EXPECT_EQ(manager.plugins().size(), 1);
    EXPECT_EQ(manager.plugin(QStringLiteral("fake")), &plugin);
    EXPECT_TRUE(manager.failures().isEmpty());
}

TEST(PluginManager, RejectsNonPlugin)
{
    PluginManager manager;
    QObject stranger;

    EXPECT_FALSE(manager.addPlugin(&stranger));
    EXPECT_TRUE(manager.plugins().isEmpty());

    // Причина должна попасть в список: молча пропавший плагин — крайне неприятная в
    // разборе неисправность.
    ASSERT_EQ(manager.failures().size(), 1);
    EXPECT_FALSE(manager.failures().first().reason.isEmpty());
}

TEST(PluginManager, RejectsMismatchedApiVersion)
{
    PluginManager manager;
    OutdatedPlugin plugin;

    EXPECT_FALSE(manager.addPlugin(&plugin));
    ASSERT_EQ(manager.failures().size(), 1);

    // Сообщение должно называть обе версии, иначе разбираться не с чем.
    const QString reason = manager.failures().first().reason;
    EXPECT_TRUE(reason.contains(QString::number(SPOTTY_API_VERSION)));
}

TEST(PluginManager, RejectsEmptyId)
{
    PluginManager manager;
    NamelessPlugin plugin;

    EXPECT_FALSE(manager.addPlugin(&plugin));
    EXPECT_TRUE(manager.plugins().isEmpty());
}

TEST(PluginManager, RejectsDuplicateId)
{
    PluginManager manager;
    FakeInterfacePlugin first;
    FakeInterfacePlugin second;

    ASSERT_TRUE(manager.addPlugin(&first));

    // Побеждает найденный раньше: благодаря этому свежую сборку плагина можно положить в
    // пользовательский каталог, и она перекроет штатную.
    EXPECT_FALSE(manager.addPlugin(&second));
    EXPECT_EQ(manager.plugin(QStringLiteral("fake")), &first);
}

TEST(PluginManager, UnknownIdReturnsNull)
{
    PluginManager manager;

    EXPECT_EQ(manager.plugin(QStringLiteral("nothing")), nullptr);
}
