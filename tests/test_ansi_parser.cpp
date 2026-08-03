/**
 * \file test_ansi_parser.cpp
 * \brief Тесты spotty::AnsiParser.
 */
#include <terminal/AnsiParser.h>

#include <QList>
#include <QStringList>

#include <gtest/gtest.h>

using namespace spotty;

namespace {

/**
 * \class RecordingSink
 * \brief Приёмник, запоминающий всё, что выдал разборщик.
 */
class RecordingSink : public AnsiParser::Sink
{
public:
    struct Fragment
    {
        QString text;
        TextStyle style;
    };

    void text(QStringView value, const TextStyle &style) override
    {
        fragments.append({value.toString(), style});
        plain += value;
    }

    void lineFeed() override { events.append(QStringLiteral("LF")); }
    void carriageReturn() override { events.append(QStringLiteral("CR")); }
    void backspace() override { events.append(QStringLiteral("BS")); }
    void tab() override { events.append(QStringLiteral("TAB")); }

    void eraseInLine(int mode) override
    {
        events.append(QStringLiteral("EL%1").arg(mode));
    }

    void eraseInDisplay(int mode) override
    {
        events.append(QStringLiteral("ED%1").arg(mode));
    }

    QList<Fragment> fragments;
    QStringList events;
    QString plain;
};

} // namespace

TEST(AnsiParser, PlainTextPassesThrough)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("hello"), sink);

    EXPECT_EQ(sink.plain, QStringLiteral("hello"));
    EXPECT_TRUE(sink.events.isEmpty());
}

TEST(AnsiParser, ControlCharactersBecomeEvents)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("a\tb\rc\nd\b"), sink);

    EXPECT_EQ(sink.plain, QStringLiteral("abcd"));
    EXPECT_EQ(sink.events, QStringList({QStringLiteral("TAB"), QStringLiteral("CR"),
                                        QStringLiteral("LF"), QStringLiteral("BS")}));
}

TEST(AnsiParser, BasicSgrColoursAreIndexed)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("\x1b[31mred\x1b[0mplain"), sink);

    ASSERT_EQ(sink.fragments.size(), 2);
    EXPECT_EQ(sink.fragments[0].text, QStringLiteral("red"));
    EXPECT_EQ(sink.fragments[0].style.foregroundSource, ColorSource::Indexed);
    EXPECT_EQ(sink.fragments[0].style.foregroundIndex, 1);

    EXPECT_EQ(sink.fragments[1].text, QStringLiteral("plain"));
    EXPECT_EQ(sink.fragments[1].style.foregroundSource, ColorSource::Default);
}

TEST(AnsiParser, BrightColoursMapToUpperHalfOfPalette)
{
    AnsiParser parser;
    RecordingSink sink;

    // 90 — яркий чёрный, то есть номер 8. Отдельного диапазона для ярких цветов в
    // палитре нет, они продолжают основной.
    parser.parse(QByteArrayLiteral("\x1b[92mx"), sink);

    ASSERT_EQ(sink.fragments.size(), 1);
    EXPECT_EQ(sink.fragments[0].style.foregroundIndex, 10);
}

TEST(AnsiParser, Indexed256Colour)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("\x1b[38;5;196mx"), sink);

    ASSERT_EQ(sink.fragments.size(), 1);
    EXPECT_EQ(sink.fragments[0].style.foregroundSource, ColorSource::Indexed);
    EXPECT_EQ(sink.fragments[0].style.foregroundIndex, 196);
}

TEST(AnsiParser, TrueColourIsPackedAsRgb)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("\x1b[38;2;18;52;86mx"), sink);

    ASSERT_EQ(sink.fragments.size(), 1);
    EXPECT_EQ(sink.fragments[0].style.foregroundSource, ColorSource::Rgb);
    EXPECT_EQ(sink.fragments[0].style.foregroundRgb, 0x123456u);
}

TEST(AnsiParser, BackgroundTrueColour)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("\x1b[48;2;255;0;128mx"), sink);

    ASSERT_EQ(sink.fragments.size(), 1);
    EXPECT_EQ(sink.fragments[0].style.backgroundSource, ColorSource::Rgb);
    EXPECT_EQ(sink.fragments[0].style.backgroundRgb, 0xFF0080u);
}

TEST(AnsiParser, TextAttributesSetAndClear)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("\x1b[1;3;4;7;9mall\x1b[22;23;24;27;29mnone"), sink);

    ASSERT_EQ(sink.fragments.size(), 2);

    const TextStyle &on = sink.fragments[0].style;
    EXPECT_TRUE(on.bold);
    EXPECT_TRUE(on.italic);
    EXPECT_TRUE(on.underline);
    EXPECT_TRUE(on.inverse);
    EXPECT_TRUE(on.strikeOut);

    const TextStyle &off = sink.fragments[1].style;
    EXPECT_FALSE(off.bold);
    EXPECT_FALSE(off.italic);
    EXPECT_FALSE(off.underline);
    EXPECT_FALSE(off.inverse);
    EXPECT_FALSE(off.strikeOut);
}

TEST(AnsiParser, EmptySgrMeansReset)
{
    AnsiParser parser;
    RecordingSink sink;

    // «ESC[m» равнозначно «ESC[0m» — так поступают многие программы, и разбирать это
    // как «нет параметров, ничего не делаем» было бы ошибкой.
    parser.parse(QByteArrayLiteral("\x1b[31ma\x1b[mb"), sink);

    ASSERT_EQ(sink.fragments.size(), 2);
    EXPECT_EQ(sink.fragments[1].style.foregroundSource, ColorSource::Default);
}

TEST(AnsiParser, EraseSequencesReachTheSink)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("\x1b[K\x1b[2K\x1b[J\x1b[2J"), sink);

    EXPECT_EQ(sink.events, QStringList({QStringLiteral("EL0"), QStringLiteral("EL2"),
                                        QStringLiteral("ED0"), QStringLiteral("ED2")}));
}

TEST(AnsiParser, UnknownCsiIsSwallowed)
{
    AnsiParser parser;
    RecordingSink sink;

    // Адресация курсора нам не нужна, но и в вывод она попадать не должна.
    parser.parse(QByteArrayLiteral("a\x1b[10;20Hb\x1b[2Ac"), sink);

    EXPECT_EQ(sink.plain, QStringLiteral("abc"));
    EXPECT_TRUE(sink.events.isEmpty());
}

TEST(AnsiParser, OscIsSwallowedUntilTerminator)
{
    AnsiParser parser;
    RecordingSink sink;

    // Заголовок окна: «ESC ] 0 ; текст BEL». Содержимое не должно вылиться в вывод.
    parser.parse(QByteArrayLiteral("a\x1b]0;window title\x07"
                                   "b"),
                 sink);

    EXPECT_EQ(sink.plain, QStringLiteral("ab"));
}

TEST(AnsiParser, OscTerminatedByStringTerminator)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("a\x1b]0;title\x1b\\b"), sink);

    EXPECT_EQ(sink.plain, QStringLiteral("ab"));
}

TEST(AnsiParser, SequenceSplitAcrossChunks)
{
    AnsiParser parser;
    RecordingSink sink;

    // Чтение из порта заканчивается где придётся, в том числе посреди последовательности.
    // Разборщик обязан достроить её следующей порцией, а не выплюнуть «[31m» в вывод.
    parser.parse(QByteArrayLiteral("plain\x1b["), sink);
    parser.parse(QByteArrayLiteral("31mred"), sink);

    EXPECT_EQ(sink.plain, QStringLiteral("plainred"));
    ASSERT_EQ(sink.fragments.size(), 2);
    EXPECT_EQ(sink.fragments[1].text, QStringLiteral("red"));
    EXPECT_EQ(sink.fragments[1].style.foregroundIndex, 1);
}

TEST(AnsiParser, EscapeAloneAtChunkBoundary)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("a\x1b"), sink);
    parser.parse(QByteArrayLiteral("[32mb"), sink);

    EXPECT_EQ(sink.plain, QStringLiteral("ab"));
    EXPECT_EQ(sink.fragments.last().style.foregroundIndex, 2);
}

TEST(AnsiParser, Utf8SplitAcrossChunks)
{
    AnsiParser parser;
    RecordingSink sink;

    // «Ж» в UTF-8 — два байта. Разрыв между ними не должен порождать замещающий символ.
    const QByteArray encoded = QStringLiteral("Ж").toUtf8();
    ASSERT_EQ(encoded.size(), 2);

    parser.parse(encoded.left(1), sink);
    parser.parse(encoded.mid(1), sink);

    EXPECT_EQ(sink.plain, QStringLiteral("Ж"));
}

TEST(AnsiParser, StyleSurvivesBetweenChunks)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("\x1b[31m"), sink);
    parser.parse(QByteArrayLiteral("red"), sink);

    ASSERT_EQ(sink.fragments.size(), 1);
    EXPECT_EQ(sink.fragments[0].style.foregroundIndex, 1);
}

TEST(AnsiParser, ResetClearsStyleAndState)
{
    AnsiParser parser;
    RecordingSink sink;

    parser.parse(QByteArrayLiteral("\x1b[31m"), sink);
    ASSERT_EQ(parser.style().foregroundSource, ColorSource::Indexed);

    parser.reset();
    EXPECT_EQ(parser.style().foregroundSource, ColorSource::Default);

    // Незавершённая последовательность тоже должна забыться, иначе следующая порция
    // достроила бы её и потеряла первые символы.
    parser.parse(QByteArrayLiteral("31mtext"), sink);
    EXPECT_EQ(sink.plain, QStringLiteral("31mtext"));
}

TEST(AnsiParser, RunawayCsiDoesNotSwallowEverything)
{
    AnsiParser parser;
    RecordingSink sink;

    // Одинокий ESC[ посреди двоичных данных не должен превратить весь дальнейший поток
    // в параметры несуществующей последовательности.
    QByteArray data = QByteArrayLiteral("\x1b[");
    data.append(QByteArray(200, '1'));
    data.append("tail");

    parser.parse(data, sink);

    EXPECT_TRUE(sink.plain.endsWith(QStringLiteral("tail")));
}

TEST(AnsiParser, Latin1Encoding)
{
    AnsiParser parser;
    parser.setEncoding(QStringConverter::Latin1);
    RecordingSink sink;

    // 0xE9 в Latin-1 — «é»; в UTF-8 этот байт сам по себе некорректен.
    parser.parse(QByteArray(1, char(0xE9)), sink);

    EXPECT_EQ(sink.plain, QStringLiteral("é"));
}
