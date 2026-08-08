/**
 * \file test_log_writer.cpp
 * \brief Тесты spotty::LogWriter.
 */
#include "support/TestSupport.h"

#include <spotty/data/LogWriter.h>

#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::TempDir;

namespace {

/// \brief Прочитать файл целиком.
QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

TEST(LogWriterStripAnsi, RemovesColourCodes)
{
    EXPECT_EQ(LogWriter::stripAnsi(QByteArrayLiteral("\x1b[31mred\x1b[0m")),
              QByteArrayLiteral("red"));
}

TEST(LogWriterStripAnsi, KeepsLineBreaks)
{
    // Переводы строк — не управляющие последовательности; без них лог перестал бы быть
    // построчным.
    EXPECT_EQ(LogWriter::stripAnsi(QByteArrayLiteral("a\r\n\x1b[Kb\n")),
              QByteArrayLiteral("a\r\nb\n"));
}

TEST(LogWriterStripAnsi, RemovesOscSequences)
{
    EXPECT_EQ(LogWriter::stripAnsi(QByteArrayLiteral("a\x1b]0;title\x07"
                                                     "b")),
              QByteArrayLiteral("ab"));
}

TEST(LogWriterStripAnsi, LeavesPlainTextAlone)
{
    const QByteArray plain = QByteArrayLiteral("nothing special here");
    EXPECT_EQ(LogWriter::stripAnsi(plain), plain);
}

TEST(LogWriterStripAnsi, HandlesTruncatedSequence)
{
    // Последовательность может быть разорвана на границе порции. Хвост отбрасывается —
    // в файле он всё равно не нужен.
    EXPECT_EQ(LogWriter::stripAnsi(QByteArrayLiteral("text\x1b[3")),
              QByteArrayLiteral("text"));
}

TEST(LogWriterStripAnsi, EmptyInput)
{
    EXPECT_TRUE(LogWriter::stripAnsi({}).isEmpty());
}

TEST(LogWriter, StartCreatesFileFromTemplate)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setFileNameTemplate(QStringLiteral("{alias}"));

    ASSERT_TRUE(writer.start(QStringLiteral("ttyUSB0"), QStringLiteral("Board1")));

    EXPECT_TRUE(writer.isRecording());
    EXPECT_EQ(QFileInfo(writer.currentFilePath()).fileName(),
              QStringLiteral("Board1.log"));
}

TEST(LogWriter, AliasFallsBackToInterfaceName)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setFileNameTemplate(QStringLiteral("{alias}"));

    ASSERT_TRUE(writer.start(QStringLiteral("ttyUSB0"), QString()));

    EXPECT_EQ(QFileInfo(writer.currentFilePath()).fileName(),
              QStringLiteral("ttyUSB0.log"));
}

TEST(LogWriter, InterfacePlaceholder)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setFileNameTemplate(QStringLiteral("{interface}"));

    ASSERT_TRUE(writer.start(QStringLiteral("COM5"), QStringLiteral("Board")));

    EXPECT_EQ(QFileInfo(writer.currentFilePath()).fileName(), QStringLiteral("COM5.log"));
}

TEST(LogWriter, ForbiddenCharactersAreSanitized)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setFileNameTemplate(QStringLiteral("{alias}"));

    // Псевдоним задаёт пользователь, и в нём легко оказывается разделитель пути.
    ASSERT_TRUE(writer.start(QStringLiteral("tty"), QStringLiteral("a/b:c*d")));

    const QString fileName = QFileInfo(writer.currentFilePath()).fileName();
    EXPECT_FALSE(fileName.contains(u'/'));
    EXPECT_FALSE(fileName.contains(u':'));
    EXPECT_FALSE(fileName.contains(u'*'));
}

TEST(LogWriter, SecondRecordingDoesNotOverwriteTheFirst)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setFileNameTemplate(QStringLiteral("fixed"));

    ASSERT_TRUE(writer.start(QStringLiteral("tty"), QString()));
    const QString first = writer.currentFilePath();
    writer.stop();

    ASSERT_TRUE(writer.start(QStringLiteral("tty"), QString()));
    const QString second = writer.currentFilePath();
    writer.stop();

    // Второй сеанс не должен дописывать в чужой лог или затирать его.
    EXPECT_NE(first, second);
    EXPECT_TRUE(QFile::exists(first));
    EXPECT_TRUE(QFile::exists(second));
}

TEST(LogWriter, WritesReceivedData)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setFilterAnsi(false);
    ASSERT_TRUE(writer.start(QStringLiteral("tty"), QString()));

    writer.write(QByteArrayLiteral("hello\n"), DataDirection::Rx);
    const QString path = writer.currentFilePath();
    writer.stop();

    EXPECT_EQ(readAll(path), QByteArrayLiteral("hello\n"));
}

TEST(LogWriter, FiltersAnsiWhenRequested)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setFilterAnsi(true);
    ASSERT_TRUE(writer.start(QStringLiteral("tty"), QString()));

    writer.write(QByteArrayLiteral("\x1b[31mred\x1b[0m\n"), DataDirection::Rx);
    const QString path = writer.currentFilePath();
    writer.stop();

    // Цветовые коды в файле мешают читать его чем угодно, кроме терминала.
    EXPECT_EQ(readAll(path), QByteArrayLiteral("red\n"));
}

TEST(LogWriter, ExcludesSentDataWhenDisabled)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setIncludeTx(false);
    ASSERT_TRUE(writer.start(QStringLiteral("tty"), QString()));

    writer.write(QByteArrayLiteral("received"), DataDirection::Rx);
    writer.write(QByteArrayLiteral("sent"), DataDirection::Tx);
    const QString path = writer.currentFilePath();
    writer.stop();

    EXPECT_EQ(readAll(path), QByteArrayLiteral("received"));
}

TEST(LogWriter, WriteWithoutRecordingIsIgnored)
{
    LogWriter writer;

    // Не должно ни падать, ни создавать файл на пустом месте.
    writer.write(QByteArrayLiteral("data"), DataDirection::Rx);

    EXPECT_FALSE(writer.isRecording());
    EXPECT_TRUE(writer.currentFilePath().isEmpty());
}

TEST(LogWriter, BytesWrittenCounts)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setFilterAnsi(false);
    ASSERT_TRUE(writer.start(QStringLiteral("tty"), QString()));

    writer.write(QByteArrayLiteral("12345"), DataDirection::Rx);

    EXPECT_EQ(writer.bytesWritten(), 5);
    writer.stop();
}

TEST(LogWriter, StopIsIdempotent)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    ASSERT_TRUE(writer.start(QStringLiteral("tty"), QString()));

    writer.stop();
    writer.stop();

    EXPECT_FALSE(writer.isRecording());
}

TEST(LogWriter, RecentLogsAreNewestFirst)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setFileNameTemplate(QStringLiteral("{alias}"));

    ASSERT_TRUE(writer.start(QStringLiteral("tty"), QStringLiteral("first")));
    const QString first = writer.currentFilePath();
    writer.stop();
    ASSERT_TRUE(writer.start(QStringLiteral("tty"), QStringLiteral("second")));
    writer.stop();

    // Оба файла созданы в одно мгновение, а разрешение времени изменения на некоторых
    // файловых системах — целая секунда. Без явного разведения времён тест проходил бы
    // через раз, что хуже, чем не иметь его вовсе.
    QFile firstFile(first);
    ASSERT_TRUE(firstFile.open(QIODevice::ReadWrite));
    ASSERT_TRUE(firstFile.setFileTime(QDateTime::currentDateTime().addSecs(-60),
                                      QFileDevice::FileModificationTime));
    firstFile.close();

    const QStringList logs = writer.recentLogs();
    ASSERT_EQ(logs.size(), 2);
    EXPECT_TRUE(QFileInfo(logs.first()).fileName().startsWith(QStringLiteral("second")));
}

TEST(LogWriter, RecentLogsRespectsLimit)
{
    TempDir dir;
    LogWriter writer;
    writer.setDirectory(dir.path());
    writer.setFileNameTemplate(QStringLiteral("{alias}"));

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(writer.start(QStringLiteral("tty"), QStringLiteral("log%1").arg(i)));
        writer.stop();
    }

    EXPECT_EQ(writer.recentLogs(3).size(), 3);
}

TEST(LogWriter, RecentLogsOnMissingDirectory)
{
    LogWriter writer;
    writer.setDirectory(QStringLiteral("/nonexistent/spotty/logs"));

    EXPECT_TRUE(writer.recentLogs().isEmpty());
}
