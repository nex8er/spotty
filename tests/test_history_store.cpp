/**
 * \file test_history_store.cpp
 * \brief Тесты spotty::HistoryStore.
 */
#include "support/TestSupport.h"

#include <HistoryStore.h>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::TempDir;

TEST(HistoryStore, MissingFileIsNotAnError)
{
    TempDir dir;
    HistoryStore history(dir.filePath(QStringLiteral("history.txt")));

    EXPECT_TRUE(history.load());
    EXPECT_TRUE(history.entries().isEmpty());
}

TEST(HistoryStore, AppendAndRoundTrip)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("history.txt"));

    {
        HistoryStore history(path);
        history.append(QStringLiteral("first"));
        history.append(QStringLiteral("second"));
        ASSERT_TRUE(history.save());
    }

    HistoryStore reloaded(path);
    ASSERT_TRUE(reloaded.load());
    EXPECT_EQ(reloaded.entries(),
              QStringList({QStringLiteral("first"), QStringLiteral("second")}));
}

TEST(HistoryStore, RepeatMovesEntryToTheEnd)
{
    TempDir dir;
    HistoryStore history(dir.filePath(QStringLiteral("history.txt")));

    history.append(QStringLiteral("a"));
    history.append(QStringLiteral("b"));
    history.append(QStringLiteral("a"));

    // Иначе после десяти проверок одной команды история состояла бы из десяти её копий,
    // и перебор стрелкой вверх стал бы бесполезен.
    EXPECT_EQ(history.entries(), QStringList({QStringLiteral("b"), QStringLiteral("a")}));
}

TEST(HistoryStore, EmptyEntryIsIgnored)
{
    TempDir dir;
    HistoryStore history(dir.filePath(QStringLiteral("history.txt")));

    history.append(QString());

    EXPECT_TRUE(history.entries().isEmpty());
}

TEST(HistoryStore, OldestEntriesAreEvicted)
{
    TempDir dir;
    HistoryStore history(dir.filePath(QStringLiteral("history.txt")), 3);

    for (int i = 0; i < 5; ++i)
        history.append(QStringLiteral("entry%1").arg(i));

    EXPECT_EQ(history.entries(), QStringList({QStringLiteral("entry2"),
                                              QStringLiteral("entry3"),
                                              QStringLiteral("entry4")}));
}

TEST(HistoryStore, CompletionReturnsLongestCommonPrefix)
{
    TempDir dir;
    HistoryStore history(dir.filePath(QStringLiteral("history.txt")));

    history.append(QStringLiteral("AT+CWMODE=1"));
    history.append(QStringLiteral("AT+CWLAP"));
    history.append(QStringLiteral("AT+CWJAP"));

    QStringList matches;
    const QString completed = history.complete(QStringLiteral("AT+"), &matches);

    // Дополнять до одного из вариантов, когда их несколько, значило бы навязать выбор,
    // которого пользователь не делал.
    EXPECT_EQ(completed, QStringLiteral("AT+CW"));
    EXPECT_EQ(matches.size(), 3);
}

TEST(HistoryStore, CompletionToFullEntryWhenUnique)
{
    TempDir dir;
    HistoryStore history(dir.filePath(QStringLiteral("history.txt")));

    history.append(QStringLiteral("AT+RST"));
    history.append(QStringLiteral("version"));

    QStringList matches;
    EXPECT_EQ(history.complete(QStringLiteral("AT"), &matches), QStringLiteral("AT+RST"));
    EXPECT_EQ(matches.size(), 1);
}

TEST(HistoryStore, CompletionWithoutMatchesReturnsPrefix)
{
    TempDir dir;
    HistoryStore history(dir.filePath(QStringLiteral("history.txt")));
    history.append(QStringLiteral("hello"));

    QStringList matches;
    EXPECT_EQ(history.complete(QStringLiteral("zzz"), &matches), QStringLiteral("zzz"));
    EXPECT_TRUE(matches.isEmpty());
}

TEST(HistoryStore, CompletionIgnoresExactMatch)
{
    TempDir dir;
    HistoryStore history(dir.filePath(QStringLiteral("history.txt")));
    history.append(QStringLiteral("reset"));

    QStringList matches;
    // Дополнять до самой себя нечем: подходят только записи длиннее набранного.
    EXPECT_EQ(history.complete(QStringLiteral("reset"), &matches), QStringLiteral("reset"));
    EXPECT_TRUE(matches.isEmpty());
}

TEST(HistoryStore, CompletionOrdersNewestFirst)
{
    TempDir dir;
    HistoryStore history(dir.filePath(QStringLiteral("history.txt")));

    history.append(QStringLiteral("cmd-old"));
    history.append(QStringLiteral("cmd-new"));

    QStringList matches;
    history.complete(QStringLiteral("cmd"), &matches);

    ASSERT_EQ(matches.size(), 2);
    EXPECT_EQ(matches.first(), QStringLiteral("cmd-new"));
}

TEST(HistoryStore, LoadTruncatesOversizedFile)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("history.txt"));

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    for (int i = 0; i < 10; ++i)
        file.write(QStringLiteral("line%1\n").arg(i).toUtf8());
    file.close();

    // Файл могли дописать руками сверх предела.
    HistoryStore history(path, 3);
    ASSERT_TRUE(history.load());

    EXPECT_EQ(history.entries().size(), 3);
    EXPECT_EQ(history.entries().last(), QStringLiteral("line9"));
}

TEST(HistoryStore, ClearEmptiesEntries)
{
    TempDir dir;
    HistoryStore history(dir.filePath(QStringLiteral("history.txt")));
    history.append(QStringLiteral("a"));

    history.clear();

    EXPECT_TRUE(history.entries().isEmpty());
}
