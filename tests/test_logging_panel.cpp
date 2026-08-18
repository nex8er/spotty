/**
 * \file test_logging_panel.cpp
 * \brief Тесты сохранения снимка буфера из панели журналирования.
 */
#include "LoggingPanel.h"

#include "support/FakePanelHost.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QTime>
#include <QToolButton>
#include <QTreeWidget>

using namespace spotty;

namespace {

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

class OpenHost final : public test::FakePanelHost
{
public:
    using FakePanelHost::FakePanelHost;

    ChannelState channelState() const override { return ChannelState::Open; }
    QString interfaceId() const override { return QStringLiteral("test-interface"); }
};

} // namespace

TEST(LoggingPanel, AddsNewRecordingToTheListImmediately)
{
    test::TempDir dir;
    OpenHost host(dir.path());
    LoggingPanel panel(&host);
    Q_EMIT host.channelStateChanged(ChannelState::Open);

    QToolButton *record = panel.findChild<QToolButton *>(QStringLiteral("recordButton"));
    LogFileList *files = panel.findChild<LogFileList *>();
    ASSERT_NE(record, nullptr);
    ASSERT_NE(files, nullptr);
    ASSERT_EQ(files->topLevelItemCount(), 0);

    record->click();

    ASSERT_EQ(files->topLevelItemCount(), 1);
    EXPECT_EQ(files->topLevelItem(0)->text(1), QStringLiteral("0 B"));

    Q_EMIT host.dataLogged(QByteArrayLiteral("payload"), DataDirection::Rx);
    EXPECT_EQ(files->topLevelItem(0)->text(1), QStringLiteral("7 B"));
}

TEST(LoggingPanel, SavesCurrentBufferWithoutRecordingNewData)
{
    test::TempDir dir;
    test::FakePanelHost host(dir.path());
    host.terminalLines = {
        {.text = QStringLiteral("received"), .raw = QByteArrayLiteral("received\n"),
         .direction = DataDirection::Rx, .complete = true},
        {.text = QStringLiteral("sent"), .raw = QByteArrayLiteral("sent\n"),
         .direction = DataDirection::Tx, .complete = true},
    };

    LoggingPanel panel(&host);
    QToolButton *save = panel.findChild<QToolButton *>(QStringLiteral("saveBufferButton"));
    ASSERT_NE(save, nullptr);
    ASSERT_TRUE(save->isEnabled());

    save->click();

    const QStringList logs = QDir(dir.path()).entryList({QStringLiteral("*.log")}, QDir::Files);
    ASSERT_EQ(logs.size(), 1);
    EXPECT_EQ(readAll(QDir(dir.path()).filePath(logs.first())), QByteArrayLiteral("received\nsent\n"));
    ASSERT_FALSE(host.statusMessages.isEmpty());
    EXPECT_TRUE(host.statusMessages.last().startsWith(QStringLiteral("Saved current buffer to")));
}

TEST(LoggingPanel, SavesTerminalGutterAsConfigured)
{
    test::TempDir dir;
    test::FakePanelHost host(dir.path());
    host.gutterSettings = {
        .showLineNumbers = true,
        .showSource = true,
        .showTimestamps = true,
        .showDirection = true,
        .timestampFormat = QStringLiteral("HH:mm:ss"),
    };
    host.terminalLines = {
        {.text = QStringLiteral("sent"), .raw = QByteArrayLiteral("sent\n"),
         .wallClock = QDateTime(QDate(2026, 8, 18), QTime(12, 34, 56)),
         .direction = DataDirection::Tx, .source = 1, .complete = true},
    };

    LoggingPanel panel(&host);
    QCheckBox *gutter = panel.findChild<QCheckBox *>(QStringLiteral("includeGutter"));
    QToolButton *save = panel.findChild<QToolButton *>(QStringLiteral("saveBufferButton"));
    ASSERT_NE(gutter, nullptr);
    ASSERT_NE(save, nullptr);
    gutter->setChecked(true);

    save->click();

    const QStringList logs = QDir(dir.path()).entryList({QStringLiteral("*.log")}, QDir::Files);
    ASSERT_EQ(logs.size(), 1);
    EXPECT_EQ(readAll(QDir(dir.path()).filePath(logs.first())),
              QByteArrayLiteral("1 B:12:34:56 < sent\n"));
}

TEST(LoggingPanel, RecordsFinalizedLinesWithGutter)
{
    test::TempDir dir;
    OpenHost host(dir.path());
    host.gutterSettings.showDirection = true;
    LoggingPanel panel(&host);
    Q_EMIT host.channelStateChanged(ChannelState::Open);

    QCheckBox *gutter = panel.findChild<QCheckBox *>(QStringLiteral("includeGutter"));
    QToolButton *record = panel.findChild<QToolButton *>(QStringLiteral("recordButton"));
    ASSERT_NE(gutter, nullptr);
    ASSERT_NE(record, nullptr);
    gutter->setChecked(true);
    record->click();

    host.terminalLines.append({.text = QStringLiteral("received"),
                               .direction = DataDirection::Rx,
                               .complete = true});
    Q_EMIT host.terminalLineFinalized(0);
    record->click();

    const QStringList logs = QDir(dir.path()).entryList({QStringLiteral("*.log")}, QDir::Files);
    ASSERT_EQ(logs.size(), 1);
    EXPECT_EQ(readAll(QDir(dir.path()).filePath(logs.first())), QByteArrayLiteral("> received\n"));
}
