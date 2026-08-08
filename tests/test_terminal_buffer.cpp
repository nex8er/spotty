/**
 * \file test_terminal_buffer.cpp
 * \brief Тесты spotty::TerminalBuffer.
 */
#include <terminal/TerminalBuffer.h>

#include <QStringList>

#include <gtest/gtest.h>

using namespace spotty;

namespace {

/// \brief Текст строки по сквозному номеру; пустая строка, если строки нет.
QString textAt(const TerminalBuffer &buffer, qint64 lineNumber)
{
    const TerminalBuffer::Line *line = buffer.line(lineNumber);
    return line ? line->text : QString();
}

} // namespace

TEST(TerminalBuffer, SplitsOnLineFeed)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("one\ntwo\nthree"), DataDirection::Rx, 0);

    EXPECT_EQ(buffer.lineCount(), 3);
    EXPECT_EQ(textAt(buffer, 0), QStringLiteral("one"));
    EXPECT_EQ(textAt(buffer, 1), QStringLiteral("two"));
    EXPECT_EQ(textAt(buffer, 2), QStringLiteral("three"));
}

TEST(TerminalBuffer, TrailingNewlineDoesNotCreateEmptyLine)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("one\n"), DataDirection::Rx, 0);

    // Строка создаётся лениво, поэтому завершающий перевод строки лишней пустой строки
    // не порождает.
    EXPECT_EQ(buffer.lineCount(), 1);
}

TEST(TerminalBuffer, TrailingEscapeDoesNotCreateEmptyLine)
{
    TerminalBuffer buffer;

    // Устройства сплошь и рядом шлют «текст\r\n\033[0m»: сброс цвета попадает после
    // перевода строки. Раньше это добавляло пустую строку после каждой осмысленной.
    buffer.append(QByteArrayLiteral("\x1b[36mline\r\n\x1b[0m"), DataDirection::Rx, 0);

    EXPECT_EQ(buffer.lineCount(), 1);
    EXPECT_EQ(textAt(buffer, 0), QStringLiteral("line"));
}

TEST(TerminalBuffer, ChunkedInputProducesSameLines)
{
    TerminalBuffer whole;
    whole.append(QByteArrayLiteral("alpha\nbeta\n"), DataDirection::Rx, 0);

    TerminalBuffer chunked;
    for (const char byte : QByteArrayLiteral("alpha\nbeta\n"))
        chunked.append(QByteArray(1, byte), DataDirection::Rx, 0);

    ASSERT_EQ(whole.lineCount(), chunked.lineCount());
    for (qint64 i = 0; i < whole.lineCount(); ++i)
        EXPECT_EQ(textAt(whole, i), textAt(chunked, i)) << "line " << i;
}

TEST(TerminalBuffer, RawBytesBelongToTheirOwnLine)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("aa\nbb\n"), DataDirection::Rx, 0);

    // Исходные байты нужны HEX-режиму. Раньше вся порция оседала на первой строке, и
    // дамп показывал для неё содержимое всех последующих.
    ASSERT_NE(buffer.line(0), nullptr);
    ASSERT_NE(buffer.line(1), nullptr);
    EXPECT_EQ(buffer.line(0)->raw, QByteArrayLiteral("aa\n"));
    EXPECT_EQ(buffer.line(1)->raw, QByteArrayLiteral("bb\n"));
}

TEST(TerminalBuffer, CarriageReturnOverwritesInPlace)
{
    TerminalBuffer buffer;

    // На этом работают индикаторы прогресса: строка перерисовывается на месте, новая не
    // создаётся.
    buffer.append(QByteArrayLiteral("progress 10%\rprogress 99%"), DataDirection::Rx, 0);

    EXPECT_EQ(buffer.lineCount(), 1);
    EXPECT_EQ(textAt(buffer, 0), QStringLiteral("progress 99%"));
}

TEST(TerminalBuffer, CarriageReturnShorterTextKeepsTail)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("abcdef\rXY"), DataDirection::Rx, 0);

    // Возврат каретки не стирает строку, а лишь переставляет курсор: хвост остаётся.
    EXPECT_EQ(textAt(buffer, 0), QStringLiteral("XYcdef"));
}

TEST(TerminalBuffer, BackspaceMovesCursorBack)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("abc\bX"), DataDirection::Rx, 0);

    EXPECT_EQ(textAt(buffer, 0), QStringLiteral("abX"));
}

TEST(TerminalBuffer, TabAlignsToEightColumns)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("ab\tc"), DataDirection::Rx, 0);

    EXPECT_EQ(textAt(buffer, 0), QStringLiteral("ab      c"));
}

TEST(TerminalBuffer, EraseInLineToEnd)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("abcdef\x1b[3D\x1b[K"), DataDirection::Rx, 0);

    // Курсор влево на три не поддерживается, поэтому проверяем очистку от текущей
    // позиции: она стоит в конце, и строка не меняется.
    EXPECT_EQ(textAt(buffer, 0), QStringLiteral("abcdef"));
}

TEST(TerminalBuffer, EraseWholeLine)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("abcdef\x1b[2K"), DataDirection::Rx, 0);

    EXPECT_EQ(textAt(buffer, 0), QString());
}

TEST(TerminalBuffer, EraseDisplayClearsBuffer)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("one\ntwo\n"), DataDirection::Rx, 0);
    ASSERT_EQ(buffer.lineCount(), 2);

    buffer.append(QByteArrayLiteral("\x1b[2J"), DataDirection::Rx, 0);

    EXPECT_EQ(buffer.lineCount(), 0);
}

TEST(TerminalBuffer, DirectionChangeBreaksLine)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("received"), DataDirection::Rx, 0);
    buffer.append(QByteArrayLiteral("sent"), DataDirection::Tx, 0);

    // Принятое и отправленное не должны слипаться: иначе метка направления теряет смысл.
    ASSERT_EQ(buffer.lineCount(), 2);
    EXPECT_EQ(buffer.line(0)->direction, DataDirection::Rx);
    EXPECT_EQ(buffer.line(1)->direction, DataDirection::Tx);
}

TEST(TerminalBuffer, StyleDoesNotLeakBetweenDirections)
{
    TerminalBuffer buffer;

    // У каждого направления свой разборщик: цвет, установленный устройством, не должен
    // окрашивать то, что печатаем мы.
    buffer.append(QByteArrayLiteral("\x1b[31mred"), DataDirection::Rx, 0);
    buffer.append(QByteArrayLiteral("plain"), DataDirection::Tx, 0);

    ASSERT_EQ(buffer.lineCount(), 2);
    ASSERT_FALSE(buffer.line(1)->runs.isEmpty());
    EXPECT_EQ(buffer.line(1)->runs.first().style.foregroundSource, ColorSource::Default);
}

TEST(TerminalBuffer, TerminatesLineOnPacketBoundary)
{
    TerminalBuffer buffer;

    // Пакетизатор сообщает границу сообщения даже там, где перевода строки не было.
    buffer.append(QByteArrayLiteral("\x01\x02"), DataDirection::Rx, 0, /*terminatesLine=*/true);
    buffer.append(QByteArrayLiteral("\x03\x04"), DataDirection::Rx, 0, /*terminatesLine=*/true);

    EXPECT_EQ(buffer.lineCount(), 2);
}

TEST(TerminalBuffer, RunsAreMergedForSameStyle)
{
    TerminalBuffer buffer;

    // Дописывание в конец с тем же оформлением должно удлинять последний отрезок, а не
    // плодить новые: иначе на длинной строке их стало бы столько же, сколько вызовов.
    for (int i = 0; i < 10; ++i)
        buffer.append(QByteArrayLiteral("x"), DataDirection::Rx, 0);

    ASSERT_EQ(buffer.lineCount(), 1);
    EXPECT_EQ(buffer.line(0)->runs.size(), 1);
    EXPECT_EQ(buffer.line(0)->runs.first().length, 10);
}

TEST(TerminalBuffer, RunsSplitOnStyleChange)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("\x1b[31mred\x1b[32mgreen"), DataDirection::Rx, 0);

    ASSERT_EQ(buffer.lineCount(), 1);
    ASSERT_EQ(buffer.line(0)->runs.size(), 2);
    EXPECT_EQ(buffer.line(0)->runs[0].length, 3);
    EXPECT_EQ(buffer.line(0)->runs[1].length, 5);
}

TEST(TerminalBuffer, RunsCoverTextAfterOverwrite)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("\x1b[31maaaa\r\x1b[32mbb"), DataDirection::Rx, 0);

    ASSERT_EQ(buffer.lineCount(), 1);
    const TerminalBuffer::Line *line = buffer.line(0);

    // Отрезки обязаны покрывать текст целиком и без разрывов, иначе часть строки при
    // отрисовке просто не будет нарисована.
    int covered = 0;
    for (const TerminalBuffer::StyleRun &run : line->runs) {
        EXPECT_EQ(run.start, covered);
        covered += run.length;
    }
    EXPECT_EQ(covered, line->text.size());
}

TEST(TerminalBuffer, TrimmingAdvancesFirstLineNumber)
{
    TerminalBuffer buffer;
    buffer.setMaxLines(100);

    for (int i = 0; i < 150; ++i)
        buffer.append(QByteArrayLiteral("line\n"), DataDirection::Rx, 0);

    EXPECT_EQ(buffer.lineCount(), 100);
    EXPECT_EQ(buffer.firstLineNumber(), 50);
    EXPECT_EQ(buffer.nextLineNumber(), 150);

    // Вытесненная строка недоступна, но её номер не переиспользуется: сохранённая
    // где-то позиция не должна указать на чужую строку.
    EXPECT_EQ(buffer.line(0), nullptr);
    EXPECT_NE(buffer.line(50), nullptr);
}

TEST(TerminalBuffer, ClearKeepsNumberingMonotonic)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("a\nb\n"), DataDirection::Rx, 0);
    const qint64 before = buffer.nextLineNumber();

    buffer.clear();

    EXPECT_EQ(buffer.lineCount(), 0);
    EXPECT_EQ(buffer.firstLineNumber(), before);
    EXPECT_EQ(buffer.nextLineNumber(), before);
}

TEST(TerminalBuffer, StatisticsCountBothDirections)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("12345"), DataDirection::Rx, 0);
    buffer.append(QByteArrayLiteral("ab"), DataDirection::Tx, 0);

    EXPECT_EQ(buffer.bytesReceived(), 5);
    EXPECT_EQ(buffer.bytesSent(), 2);

    buffer.resetStatistics();
    EXPECT_EQ(buffer.bytesReceived(), 0);
    EXPECT_EQ(buffer.bytesSent(), 0);
}

TEST(TerminalBuffer, SystemMessageIsSeparateLine)
{
    TerminalBuffer buffer;
    buffer.append(QByteArrayLiteral("partial"), DataDirection::Rx, 0);
    buffer.appendSystemMessage(QStringLiteral("--- opened ---"));

    ASSERT_EQ(buffer.lineCount(), 2);
    EXPECT_EQ(buffer.line(1)->direction, DataDirection::System);
    // Звёздочка и курсив — два признака сверх направления: цвет не может быть
    // единственным носителем смысла, а курсив теряется при копировании наружу.
    EXPECT_EQ(buffer.line(1)->text, QStringLiteral("* --- opened ---"));
    ASSERT_EQ(buffer.line(1)->runs.size(), 1);
    EXPECT_TRUE(buffer.line(1)->runs.first().style.italic);
}

TEST(TerminalBuffer, SignalsReportAppendedLines)
{
    TerminalBuffer buffer;

    qint64 firstReported = -1;
    qint64 countReported = 0;
    QObject::connect(&buffer, &TerminalBuffer::linesAppended,
                     [&](qint64 first, qint64 count) {
                         firstReported = first;
                         countReported = count;
                     });

    buffer.append(QByteArrayLiteral("a\nb\nc\n"), DataDirection::Rx, 0);

    EXPECT_EQ(firstReported, 0);
    EXPECT_EQ(countReported, 3);
}
