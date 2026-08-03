/**
 * \file test_settings_store.cpp
 * \brief Тесты spotty::SettingsStore.
 */
#include "support/TestSupport.h"

#include <settings/SettingsStore.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <QStringList>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::TempDir;
using spotty::test::waitFor;

TEST(SettingsStore, MissingFileIsNotAnError)
{
    TempDir dir;
    SettingsStore store(dir.filePath(QStringLiteral("settings.json")));

    // Первый запуск: действуют умолчания, файл появится при первой записи.
    EXPECT_TRUE(store.load());
    EXPECT_TRUE(store.data().isEmpty());
}

TEST(SettingsStore, NestedKeysMapToNestedObjects)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    SettingsStore store(path);
    store.setValue(QStringLiteral("terminal/fontSize"), 14);
    ASSERT_TRUE(store.save());

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    ASSERT_TRUE(root.contains(QStringLiteral("terminal")));
    EXPECT_EQ(root.value(QStringLiteral("terminal")).toObject()
                  .value(QStringLiteral("fontSize")).toInt(),
              14);
}

TEST(SettingsStore, RoundTripPreservesTypes)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    {
        SettingsStore store(path);
        store.setValue(QStringLiteral("a/number"), 42);
        store.setValue(QStringLiteral("a/flag"), true);
        store.setValue(QStringLiteral("a/text"), QStringLiteral("hello"));
        store.setValue(QStringLiteral("a/list"), QStringList({QStringLiteral("x")}));
        ASSERT_TRUE(store.save());
    }

    SettingsStore reloaded(path);
    ASSERT_TRUE(reloaded.load());

    EXPECT_EQ(reloaded.value(QStringLiteral("a/number")).toInt(), 42);
    EXPECT_TRUE(reloaded.value(QStringLiteral("a/flag")).toBool());
    EXPECT_EQ(reloaded.value(QStringLiteral("a/text")).toString(), QStringLiteral("hello"));
    EXPECT_EQ(reloaded.value(QStringLiteral("a/list")).toStringList(),
              QStringList({QStringLiteral("x")}));
}

TEST(SettingsStore, FallbackForMissingKey)
{
    TempDir dir;
    SettingsStore store(dir.filePath(QStringLiteral("settings.json")));

    EXPECT_EQ(store.value(QStringLiteral("missing/key"), 7).toInt(), 7);
    EXPECT_FALSE(store.contains(QStringLiteral("missing/key")));
}

TEST(SettingsStore, DeepNesting)
{
    TempDir dir;
    SettingsStore store(dir.filePath(QStringLiteral("settings.json")));

    store.setValue(QStringLiteral("a/b/c/d"), QStringLiteral("deep"));

    EXPECT_EQ(store.value(QStringLiteral("a/b/c/d")).toString(), QStringLiteral("deep"));
    EXPECT_TRUE(store.contains(QStringLiteral("a/b/c/d")));
}

TEST(SettingsStore, RemoveDeletesKey)
{
    TempDir dir;
    SettingsStore store(dir.filePath(QStringLiteral("settings.json")));

    store.setValue(QStringLiteral("a/b"), 1);
    ASSERT_TRUE(store.contains(QStringLiteral("a/b")));

    store.remove(QStringLiteral("a/b"));

    EXPECT_FALSE(store.contains(QStringLiteral("a/b")));
}

TEST(SettingsStore, GroupReadAndWrite)
{
    TempDir dir;
    SettingsStore store(dir.filePath(QStringLiteral("settings.json")));

    QVariantMap group;
    group.insert(QStringLiteral("baudRate"), 115200);
    group.insert(QStringLiteral("parity"), QStringLiteral("N"));
    store.setGroup(QStringLiteral("uart:1"), group);

    EXPECT_EQ(store.group(QStringLiteral("uart:1")), group);
}

TEST(SettingsStore, SettingSameValueEmitsNothing)
{
    TempDir dir;
    SettingsStore store(dir.filePath(QStringLiteral("settings.json")));
    store.setValue(QStringLiteral("a"), 1);

    int changes = 0;
    QObject::connect(&store, &SettingsStore::valueChanged, [&] { ++changes; });

    // Без этой проверки перестроение списка интерфейсов раз в секунду дёргало бы диск,
    // не меняя при этом ни байта.
    store.setValue(QStringLiteral("a"), 1);

    EXPECT_EQ(changes, 0);
}

TEST(SettingsStore, ValueChangedIsEmittedOnChange)
{
    TempDir dir;
    SettingsStore store(dir.filePath(QStringLiteral("settings.json")));

    QString changedKey;
    QObject::connect(&store, &SettingsStore::valueChanged,
                     [&](const QString &key, const QVariant &) { changedKey = key; });

    store.setValue(QStringLiteral("section/item"), 5);

    EXPECT_EQ(changedKey, QStringLiteral("section/item"));
}

TEST(SettingsStore, MalformedJsonLeavesFileUntouched)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    const QByteArray broken = QByteArrayLiteral("{ this is not json");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(broken);
    file.close();

    SettingsStore store(path);
    EXPECT_FALSE(store.load());
    EXPECT_TRUE(store.data().isEmpty());

    // Файл — единственная копия того, что настроил пользователь. Затирать его нельзя:
    // человек может захотеть починить его руками.
    QFile check(path);
    ASSERT_TRUE(check.open(QIODevice::ReadOnly));
    EXPECT_EQ(check.readAll(), broken);
}

TEST(SettingsStore, DebouncedSaveEventuallyWritesFile)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    SettingsStore store(path);
    store.setValue(QStringLiteral("a"), 1);

    // Запись отложена, чтобы перетаскивание ползунка не било по диску сотню раз.
    EXPECT_TRUE(waitFor([&] { return QFile::exists(path); }));
}

TEST(SettingsStore, DestructorFlushesPendingChanges)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    {
        SettingsStore store(path);
        store.setValue(QStringLiteral("a"), 99);
        // Уходим не дожидаясь таймера: несохранённое дописывает деструктор.
    }

    SettingsStore reloaded(path);
    ASSERT_TRUE(reloaded.load());
    EXPECT_EQ(reloaded.value(QStringLiteral("a")).toInt(), 99);
}

TEST(SettingsStore, EmptyKeyIsIgnored)
{
    TempDir dir;
    SettingsStore store(dir.filePath(QStringLiteral("settings.json")));

    store.setValue(QString(), 1);
    store.setValue(QStringLiteral("///"), 1);

    EXPECT_TRUE(store.data().isEmpty());
}
