/**
 * \file test_app_settings.cpp
 * \brief Тесты spotty::AppSettings.
 */
#include "support/TestSupport.h"

#include <settings/AppSettings.h>
#include <settings/Paths.h>
#include <settings/SettingsStore.h>

#include <QStringList>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::TempDir;

TEST(AppSettings, DefaultsOnEmptyStore)
{
    TempDir dir;
    SettingsStore store(dir.filePath(QStringLiteral("settings.json")));

    const AppSettings settings = AppSettings::load(store);

    EXPECT_EQ(settings.language, QStringLiteral("system"));
    EXPECT_EQ(settings.theme, QStringLiteral("dark"));
    EXPECT_EQ(settings.maxLines, 20000);
    EXPECT_EQ(settings.hexBytesPerRow, 16);
    EXPECT_EQ(settings.sendTermination, 3);
    EXPECT_EQ(settings.sendTarget, 0);
    EXPECT_TRUE(settings.localEcho);
    EXPECT_EQ(settings.hiddenSources, 0);

    // Открытие порта дёргает DTR и перехватывает порт у другой программы: делать это молча
    // при каждом запуске нельзя.
    EXPECT_FALSE(settings.autoOpenLastInterface);
}

TEST(AppSettings, RoundTripThroughStore)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    AppSettings original;
    original.language = QStringLiteral("ru");
    original.theme = QStringLiteral("light");
    original.autoOpenLastInterface = true;
    original.singleInstance = false;
    original.fontFamily = QStringLiteral("Menlo");
    original.fontSize = 13;
    original.maxLines = 5000;
    original.showTimestamps = true;
    original.relativeTimestamps = true;
    original.timestampFormat = QStringLiteral("mm:ss");
    original.showDirection = false;
    original.localEcho = false;
    original.hexBytesPerRow = 8;
    original.viewMode = QStringLiteral("hex");
    original.encoding = QStringLiteral("latin1");
    original.ansiPalette = QStringList(16, QStringLiteral("#112233"));
    original.sendFormat = 1;
    original.sendTermination = 2;
    original.sendTarget = 2;
    original.historySize = 42;
    original.packetizerMode = 2;
    original.packetizerTimeoutMs = 55;
    original.packetizerDelimiterHex = QStringLiteral("0D0A");
    original.packetizerFixedLength = 32;
    // Скрытые транспорты: бит на источник. Скрывают показ, но не разбор — строки
    // остаются в буфере и достаются плагинам.
    original.hiddenSources = 0b10;
    original.shortcuts.insert(QStringLiteral("terminal.clear"), QStringLiteral("Ctrl+K"));

    {
        SettingsStore store(path);
        original.save(store);
        ASSERT_TRUE(store.save());
    }

    SettingsStore reloaded(path);
    ASSERT_TRUE(reloaded.load());
    const AppSettings restored = AppSettings::load(reloaded);

    EXPECT_EQ(restored.hiddenSources, original.hiddenSources);
    EXPECT_EQ(restored.language, original.language);
    EXPECT_EQ(restored.theme, original.theme);
    EXPECT_EQ(restored.autoOpenLastInterface, original.autoOpenLastInterface);
    EXPECT_EQ(restored.singleInstance, original.singleInstance);
    EXPECT_EQ(restored.fontFamily, original.fontFamily);
    EXPECT_EQ(restored.fontSize, original.fontSize);
    EXPECT_EQ(restored.maxLines, original.maxLines);
    EXPECT_EQ(restored.showTimestamps, original.showTimestamps);
    EXPECT_EQ(restored.relativeTimestamps, original.relativeTimestamps);
    EXPECT_EQ(restored.timestampFormat, original.timestampFormat);
    EXPECT_EQ(restored.showDirection, original.showDirection);
    EXPECT_EQ(restored.localEcho, original.localEcho);
    EXPECT_EQ(restored.hexBytesPerRow, original.hexBytesPerRow);
    EXPECT_EQ(restored.viewMode, original.viewMode);
    EXPECT_EQ(restored.encoding, original.encoding);
    EXPECT_EQ(restored.ansiPalette, original.ansiPalette);
    EXPECT_EQ(restored.sendFormat, original.sendFormat);
    EXPECT_EQ(restored.sendTermination, original.sendTermination);
    EXPECT_EQ(restored.sendTarget, original.sendTarget);
    EXPECT_EQ(restored.historySize, original.historySize);
    EXPECT_EQ(restored.packetizerMode, original.packetizerMode);
    EXPECT_EQ(restored.packetizerTimeoutMs, original.packetizerTimeoutMs);
    EXPECT_EQ(restored.packetizerDelimiterHex, original.packetizerDelimiterHex);
    EXPECT_EQ(restored.packetizerFixedLength, original.packetizerFixedLength);
    EXPECT_EQ(restored.shortcuts, original.shortcuts);
}

TEST(AppSettings, EmptyPaletteMeansFollowTheme)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.json"));

    AppSettings settings;
    ASSERT_TRUE(settings.ansiPalette.isEmpty());

    {
        SettingsStore store(path);
        settings.save(store);
        ASSERT_TRUE(store.save());
    }

    SettingsStore reloaded(path);
    ASSERT_TRUE(reloaded.load());

    // Пустой список должен пережить запись и чтение: иначе тема, выбранная случайно при
    // первом запуске, намертво зафиксировала бы цвета.
    EXPECT_TRUE(AppSettings::load(reloaded).ansiPalette.isEmpty());
}

TEST(AppSettings, UnknownKeysInStoreAreIgnored)
{
    TempDir dir;
    SettingsStore store(dir.filePath(QStringLiteral("settings.json")));
    store.setValue(QStringLiteral("some/leftover"), QStringLiteral("value"));

    // Ключ от старой версии не должен мешать чтению остальных настроек.
    const AppSettings settings = AppSettings::load(store);
    EXPECT_EQ(settings.maxLines, 20000);
}
