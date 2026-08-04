/**
 * \file test_macro_store.cpp
 * \brief Тесты spotty::MacroStore.
 */
#include "support/TestSupport.h"

#include <MacroStore.h>

#include <QFile>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::TempDir;

namespace {

Macro makeMacro(const QString &payload)
{
    Macro macro;
    macro.payload = payload;
    macro.format = DataCodec::Format::Text;
    macro.termination = DataCodec::Termination::CrLf;
    return macro;
}

} // namespace

TEST(MacroStore, EmptyDirectoryHasNoPresets)
{
    TempDir dir;
    MacroStore store(dir.path());

    EXPECT_TRUE(store.presets().isEmpty());
}

TEST(MacroStore, CreateAndListPresets)
{
    TempDir dir;
    MacroStore store(dir.path());

    ASSERT_TRUE(store.createPreset(QStringLiteral("board")));
    ASSERT_TRUE(store.createPreset(QStringLiteral("modem")));

    EXPECT_EQ(store.presets(),
              QStringList({QStringLiteral("board"), QStringLiteral("modem")}));
}

TEST(MacroStore, PresetIsASeparateFile)
{
    TempDir dir;
    MacroStore store(dir.path());
    ASSERT_TRUE(store.createPreset(QStringLiteral("board")));

    // Файл на пресет — чтобы набор для конкретного устройства переносился одним файлом.
    EXPECT_TRUE(QFile::exists(dir.filePath(QStringLiteral("board.json"))));
}

TEST(MacroStore, RoundTripPreservesEveryField)
{
    TempDir dir;

    {
        MacroStore store(dir.path());
        ASSERT_TRUE(store.createPreset(QStringLiteral("set")));

        Macro macro = makeMacro(QStringLiteral("AT+RST"));
        macro.format = DataCodec::Format::Hex;
        macro.termination = DataCodec::Termination::Lf;
        macro.shortcut = QStringLiteral("Ctrl+1");
        store.macros().append(macro);

        ASSERT_TRUE(store.save());
    }

    MacroStore reloaded(dir.path());
    ASSERT_TRUE(reloaded.loadPreset(QStringLiteral("set")));

    ASSERT_EQ(reloaded.macros().size(), 1);
    const Macro &macro = reloaded.macros().first();
    EXPECT_EQ(macro.payload, QStringLiteral("AT+RST"));
    EXPECT_EQ(macro.format, DataCodec::Format::Hex);
    EXPECT_EQ(macro.termination, DataCodec::Termination::Lf);
    EXPECT_EQ(macro.shortcut, QStringLiteral("Ctrl+1"));
}

TEST(MacroStore, MacroEncodesThroughCodec)
{
    Macro macro = makeMacro(QStringLiteral("AA 55"));
    macro.format = DataCodec::Format::Hex;
    macro.termination = DataCodec::Termination::None;

    QString error;
    EXPECT_EQ(macro.encode(&error), QByteArray::fromHex("AA55"));
    EXPECT_TRUE(error.isEmpty());
}

TEST(MacroStore, LoadingMissingPresetIsNotAnError)
{
    TempDir dir;
    MacroStore store(dir.path());

    // Набор ещё не создан — это нормально при первом запуске.
    EXPECT_TRUE(store.loadPreset(QStringLiteral("nothing")));
    EXPECT_TRUE(store.macros().isEmpty());
    EXPECT_EQ(store.currentPreset(), QStringLiteral("nothing"));
}

TEST(MacroStore, DuplicatePresetIsRejected)
{
    TempDir dir;
    MacroStore store(dir.path());
    ASSERT_TRUE(store.createPreset(QStringLiteral("board")));

    EXPECT_FALSE(store.createPreset(QStringLiteral("board")));
}

TEST(MacroStore, PathTraversalIsRejected)
{
    TempDir dir;
    MacroStore store(dir.path());

    // Имя набора приходит от пользователя и не должно позволять писать за пределы
    // каталога макросов.
    EXPECT_FALSE(store.createPreset(QStringLiteral("../escape")));
    EXPECT_FALSE(store.createPreset(QStringLiteral("sub/dir")));
    EXPECT_FALSE(store.createPreset(QStringLiteral(".hidden")));
    EXPECT_FALSE(store.createPreset(QString()));
    EXPECT_FALSE(store.createPreset(QStringLiteral("bad:name")));
    EXPECT_FALSE(store.createPreset(QStringLiteral("bad*name")));

    EXPECT_TRUE(store.presets().isEmpty());
}

TEST(MacroStore, OverlongNameIsRejected)
{
    TempDir dir;
    MacroStore store(dir.path());

    EXPECT_FALSE(store.createPreset(QString(200, u'a')));
}

TEST(MacroStore, DeletePresetRemovesFile)
{
    TempDir dir;
    MacroStore store(dir.path());
    ASSERT_TRUE(store.createPreset(QStringLiteral("temp")));
    ASSERT_TRUE(QFile::exists(dir.filePath(QStringLiteral("temp.json"))));

    EXPECT_TRUE(store.deletePreset(QStringLiteral("temp")));

    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("temp.json"))));
    EXPECT_TRUE(store.presets().isEmpty());
    EXPECT_TRUE(store.macros().isEmpty());
}

TEST(MacroStore, DeletingMissingPresetFails)
{
    TempDir dir;
    MacroStore store(dir.path());

    EXPECT_FALSE(store.deletePreset(QStringLiteral("nothing")));
}

TEST(MacroStore, BareArrayFormatIsAccepted)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("hand.json"));

    // Голый массив проще писать руками, объект с полем macros оставляет место для
    // будущих полей набора. Читать нужно оба.
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral(R"([{"name":"A","payload":"x"}])"));
    file.close();

    MacroStore store(dir.path());
    ASSERT_TRUE(store.loadPreset(QStringLiteral("hand")));

    ASSERT_EQ(store.macros().size(), 1);
    EXPECT_EQ(store.macros().first().payload, QStringLiteral("x"));
}

TEST(MacroStore, MalformedJsonDoesNotDestroyFile)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("broken.json"));

    const QByteArray broken = QByteArrayLiteral("{ not json");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(broken);
    file.close();

    MacroStore store(dir.path());
    EXPECT_FALSE(store.loadPreset(QStringLiteral("broken")));

    QFile check(path);
    ASSERT_TRUE(check.open(QIODevice::ReadOnly));
    EXPECT_EQ(check.readAll(), broken);
}

TEST(MacroStore, LegacyNameBecomesPayloadWhenPayloadIsMissing)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("old.json"));

    // Наборы прежних версий хранили отдельное имя. Если команды в записи нет, командой
    // было имя — иначе такой макрос молча превратился бы в пустую строку.
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral(R"([{"name":"AT+GMR"}])"));
    file.close();

    MacroStore store(dir.path());
    ASSERT_TRUE(store.loadPreset(QStringLiteral("old")));

    ASSERT_EQ(store.macros().size(), 1);
    EXPECT_EQ(store.macros().first().payload, QStringLiteral("AT+GMR"));
}

TEST(MacroStore, SuggestedShortcutTakesFirstFreeFunctionKey)
{
    TempDir dir;
    MacroStore store(dir.path());
    ASSERT_TRUE(store.createPreset(QStringLiteral("set")));

    EXPECT_EQ(store.suggestShortcut(), QStringLiteral("F1"));

    Macro first = makeMacro(QStringLiteral("a"));
    first.shortcut = QStringLiteral("F1");
    store.macros().append(first);
    EXPECT_EQ(store.suggestShortcut(), QStringLiteral("F2"));

    // Пропуск в середине занимается раньше, чем следующая по порядку.
    Macro third = makeMacro(QStringLiteral("c"));
    third.shortcut = QStringLiteral("F3");
    store.macros().append(third);
    EXPECT_EQ(store.suggestShortcut(), QStringLiteral("F2"));
}

TEST(MacroStore, SuggestedShortcutIsEmptyWhenAllFunctionKeysAreTaken)
{
    TempDir dir;
    MacroStore store(dir.path());
    ASSERT_TRUE(store.createPreset(QStringLiteral("full")));

    for (int i = 1; i <= 12; ++i) {
        Macro macro = makeMacro(QStringLiteral("cmd%1").arg(i));
        macro.shortcut = QStringLiteral("F%1").arg(i);
        store.macros().append(macro);
    }

    // Назначать тринадцатому «Ctrl+Shift+F1» программа не вправе: такое сочетание
    // выбирает пользователь.
    EXPECT_TRUE(store.suggestShortcut().isEmpty());
}

TEST(MacroStore, ImportAppendsToCurrentPreset)
{
    TempDir dir;
    MacroStore store(dir.path());
    ASSERT_TRUE(store.createPreset(QStringLiteral("mine")));
    store.macros().append(makeMacro(QStringLiteral("own")));

    const QString incoming = dir.filePath(QStringLiteral("incoming.json"));
    {
        MacroStore other(dir.path());
        ASSERT_TRUE(other.createPreset(QStringLiteral("other")));
        other.macros().append(makeMacro(QStringLiteral("theirs")));
        ASSERT_TRUE(other.exportTo(incoming));
    }

    int added = 0;
    ASSERT_TRUE(store.importFrom(incoming, &added));

    // Именно дописать: импорт нужен, чтобы принести чужие команды к своим.
    EXPECT_EQ(added, 1);
    ASSERT_EQ(store.macros().size(), 2);
    EXPECT_EQ(store.macros().at(0).payload, QStringLiteral("own"));
    EXPECT_EQ(store.macros().at(1).payload, QStringLiteral("theirs"));
}

TEST(MacroStore, ImportDropsShortcutsAlreadyTaken)
{
    TempDir dir;
    MacroStore store(dir.path());
    ASSERT_TRUE(store.createPreset(QStringLiteral("mine")));

    Macro own = makeMacro(QStringLiteral("own"));
    own.shortcut = QStringLiteral("F1");
    store.macros().append(own);

    const QString incoming = dir.filePath(QStringLiteral("incoming.json"));
    {
        MacroStore other(dir.path());
        ASSERT_TRUE(other.createPreset(QStringLiteral("other")));
        Macro clash = makeMacro(QStringLiteral("clash"));
        clash.shortcut = QStringLiteral("F1");
        Macro free = makeMacro(QStringLiteral("free"));
        free.shortcut = QStringLiteral("F5");
        other.macros().append(clash);
        other.macros().append(free);
        ASSERT_TRUE(other.exportTo(incoming));
    }

    ASSERT_TRUE(store.importFrom(incoming));

    ASSERT_EQ(store.macros().size(), 3);
    // Занятое сочетание снимается: одно нажатие не может отвечать за две команды.
    EXPECT_TRUE(store.macros().at(1).shortcut.isEmpty());
    // Свободное сохраняется.
    EXPECT_EQ(store.macros().at(2).shortcut, QStringLiteral("F5"));
}

TEST(MacroStore, ImportOfMissingFileFails)
{
    TempDir dir;
    MacroStore store(dir.path());
    ASSERT_TRUE(store.createPreset(QStringLiteral("mine")));

    EXPECT_FALSE(store.importFrom(dir.filePath(QStringLiteral("nothing.json"))));
}

TEST(MacroStore, ExportWritesReadableFile)
{
    TempDir dir;
    MacroStore store(dir.path());
    ASSERT_TRUE(store.createPreset(QStringLiteral("set")));
    store.macros().append(makeMacro(QStringLiteral("AT")));

    const QString path = dir.filePath(QStringLiteral("out.json"));
    ASSERT_TRUE(store.exportTo(path));

    MacroStore reader(dir.path());
    ASSERT_TRUE(reader.createPreset(QStringLiteral("empty")));
    int added = 0;
    ASSERT_TRUE(reader.importFrom(path, &added));

    EXPECT_EQ(added, 1);
    EXPECT_EQ(reader.macros().first().payload, QStringLiteral("AT"));
}

TEST(MacroStore, DefaultPresetNameIsUsable)
{
    TempDir dir;
    MacroStore store(dir.path());

    EXPECT_TRUE(store.createPreset(MacroStore::defaultPresetName()));
}
