/**
 * \file test_highlight_rules.cpp
 * \brief Тесты spotty::HighlightRules.
 */
#include <HighlightRules.h>

#include <gtest/gtest.h>

using namespace spotty;

namespace {

HighlightRule makeRule(const QString &pattern, quint32 color, bool enabled = true,
                       bool caseSensitive = false)
{
    HighlightRule rule;
    rule.pattern = pattern;
    rule.color = color;
    rule.enabled = enabled;
    rule.caseSensitive = caseSensitive;
    return rule;
}

} // namespace

TEST(HighlightRules, EmptyByDefault)
{
    HighlightRules rules;

    EXPECT_TRUE(rules.isEmpty());
    EXPECT_EQ(rules.match(QStringLiteral("anything")), -1);
}

TEST(HighlightRules, MatchesSimplePattern)
{
    HighlightRules rules;
    rules.setRules({makeRule(QStringLiteral("ERROR"), 0xFF0000)});

    EXPECT_EQ(rules.match(QStringLiteral("an ERROR happened")), 0);
    EXPECT_EQ(rules.match(QStringLiteral("all fine")), -1);
}

TEST(HighlightRules, FirstEnabledRuleWins)
{
    HighlightRules rules;
    rules.setRules({
        makeRule(QStringLiteral("WARN"), 0xFFFF00),
        makeRule(QStringLiteral("."), 0x00FF00), // подходит ко всему
    });

    // Порядок задаёт пользователь: частное правило ставится выше общего, и придумывать
    // приоритеты не приходится.
    EXPECT_EQ(rules.match(QStringLiteral("WARN: low battery")), 0);
    EXPECT_EQ(rules.match(QStringLiteral("plain line")), 1);
}

TEST(HighlightRules, DisabledRuleIsSkipped)
{
    HighlightRules rules;
    rules.setRules({
        makeRule(QStringLiteral("ERROR"), 0xFF0000, /*enabled=*/false),
        makeRule(QStringLiteral("ERROR"), 0x00FF00),
    });

    EXPECT_EQ(rules.match(QStringLiteral("ERROR")), 1);
}

TEST(HighlightRules, CaseInsensitiveByDefault)
{
    HighlightRules rules;
    rules.setRules({makeRule(QStringLiteral("error"), 0xFF0000)});

    EXPECT_EQ(rules.match(QStringLiteral("ERROR")), 0);
}

TEST(HighlightRules, CaseSensitiveWhenRequested)
{
    HighlightRules rules;
    rules.setRules({makeRule(QStringLiteral("error"), 0xFF0000, true, /*caseSensitive=*/true)});

    EXPECT_EQ(rules.match(QStringLiteral("ERROR")), -1);
    EXPECT_EQ(rules.match(QStringLiteral("error")), 0);
}

TEST(HighlightRules, InvalidPatternIsIgnoredNotFatal)
{
    HighlightRules rules;
    rules.setRules({
        makeRule(QStringLiteral("[unclosed"), 0xFF0000),
        makeRule(QStringLiteral("ok"), 0x00FF00),
    });

    // Отвергать некорректное выражение при вводе нельзя: пользователь набирает по одному
    // символу, и половина набранного почти всегда некорректна.
    EXPECT_EQ(rules.match(QStringLiteral("[unclosed")), -1);
    EXPECT_EQ(rules.match(QStringLiteral("ok")), 1);
}

TEST(HighlightRules, EmptyPatternIsIgnored)
{
    HighlightRules rules;
    rules.setRules({makeRule(QString(), 0xFF0000)});

    // Пустое выражение подошло бы к любой строке и раскрасило бы весь вывод.
    EXPECT_TRUE(rules.isEmpty());
    EXPECT_EQ(rules.match(QStringLiteral("anything")), -1);
}

TEST(HighlightRules, RegularExpressionSyntaxWorks)
{
    HighlightRules rules;
    rules.setRules({makeRule(QStringLiteral("rssi=-[5-9]\\d"), 0x0000FF)});

    EXPECT_EQ(rules.match(QStringLiteral("frame 1 rssi=-72 dBm")), 0);
    EXPECT_EQ(rules.match(QStringLiteral("frame 1 rssi=-41 dBm")), -1);
}

TEST(HighlightRules, ColourLookupByIndex)
{
    HighlightRules rules;
    rules.setRules({makeRule(QStringLiteral("a"), 0x123456)});

    EXPECT_EQ(rules.colorAt(0), 0x123456u);
    EXPECT_EQ(rules.colorAt(-1), 0u);
    EXPECT_EQ(rules.colorAt(99), 0u);
}

TEST(HighlightRules, IndexRefersToOriginalListNotCompiled)
{
    HighlightRules rules;
    rules.setRules({
        makeRule(QStringLiteral("skipped"), 0x111111, /*enabled=*/false),
        makeRule(QStringLiteral("found"), 0x222222),
    });

    // Выключенные правила не компилируются, но индекс обязан указывать в исходный список,
    // иначе цвет достался бы чужому правилу.
    const int index = rules.match(QStringLiteral("found"));
    ASSERT_EQ(index, 1);
    EXPECT_EQ(rules.colorAt(index), 0x222222u);
}

TEST(HighlightRules, VariantRoundTrip)
{
    HighlightRules original;
    original.setRules({
        makeRule(QStringLiteral("ERROR"), 0xD26B6B, true, true),
        makeRule(QStringLiteral("WARN"), 0xB38400, false, false),
    });

    HighlightRules restored;
    restored.fromVariant(original.toVariant());

    ASSERT_EQ(restored.rules().size(), 2);
    EXPECT_EQ(restored.rules()[0].pattern, QStringLiteral("ERROR"));
    EXPECT_EQ(restored.rules()[0].color, 0xD26B6Bu);
    EXPECT_TRUE(restored.rules()[0].caseSensitive);
    EXPECT_TRUE(restored.rules()[0].enabled);
    EXPECT_FALSE(restored.rules()[1].enabled);
}

TEST(HighlightRules, EmptyVariantGivesEmptyRules)
{
    HighlightRules rules;
    rules.fromVariant({});

    EXPECT_TRUE(rules.isEmpty());
    EXPECT_TRUE(rules.rules().isEmpty());
}
