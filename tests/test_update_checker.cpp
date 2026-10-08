/**
 * \file test_update_checker.cpp
 * \brief Тесты spotty::UpdateChecker: сравнение версий, разбор ответа GitHub, выбор пакета.
 */
#include "support/TestSupport.h"

#include <UpdateChecker.h>

#include <QFile>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::TempDir;

namespace {

/// Ответ `releases/latest` с пакетами всех систем — как их называет CI.
QByteArray sampleRelease(const QString &tag = QStringLiteral("v0.9.0"))
{
    const QString json = QStringLiteral(R"({
        "tag_name": "%1",
        "html_url": "https://github.com/nex8er/spotty/releases/tag/%1",
        "body": "Notes",
        "assets": [
            {"name": "Spotty-0.9.0-Windows-x64.zip",
             "browser_download_url": "https://example.test/win.zip"},
            {"name": "Spotty-0.9.0-Windows-x64-Setup.exe",
             "browser_download_url": "https://example.test/win-setup.exe"},
            {"name": "Spotty-0.9.0-macOS-arm64.dmg",
             "browser_download_url": "https://example.test/mac-arm.dmg"},
            {"name": "Spotty-0.9.0-macOS-x86_64.dmg",
             "browser_download_url": "https://example.test/mac-x64.dmg"},
            {"name": "Spotty-0.9.0-Linux-x86_64.AppImage",
             "browser_download_url": "https://example.test/linux-x64.AppImage"},
            {"name": "Spotty-0.9.0-Linux-aarch64.AppImage",
             "browser_download_url": "https://example.test/linux-arm.AppImage"}
        ]
    })").arg(tag);
    return json.toUtf8();
}

} // namespace

TEST(UpdateChecker, CompareVersionsIsNumeric)
{
    EXPECT_GT(UpdateChecker::compareVersions(QStringLiteral("0.10.0"), QStringLiteral("0.9.0")), 0);
    EXPECT_LT(UpdateChecker::compareVersions(QStringLiteral("0.8.0"), QStringLiteral("0.8.1")), 0);
    EXPECT_EQ(UpdateChecker::compareVersions(QStringLiteral("v1.2.3"), QStringLiteral("1.2.3")), 0);
    // Хвост предварительной версии отбрасывается, а не ломает разбор.
    EXPECT_EQ(UpdateChecker::compareVersions(QStringLiteral("1.0.0-rc1"), QStringLiteral("1.0.0")),
              0);
}

TEST(UpdateChecker, ParsePicksPackageForPlatform)
{
    const auto windows = UpdateChecker::parseRelease(sampleRelease(), "windows", "x64");
    ASSERT_TRUE(windows);
    EXPECT_EQ(windows->version, QStringLiteral("0.9.0"));
    EXPECT_EQ(windows->tag, QStringLiteral("v0.9.0"));
    // Установщик, а не переносной .zip.
    EXPECT_EQ(windows->assetUrl, QUrl(QStringLiteral("https://example.test/win-setup.exe")));

    const auto macArm = UpdateChecker::parseRelease(sampleRelease(), "macos", "arm64");
    ASSERT_TRUE(macArm);
    EXPECT_EQ(macArm->assetUrl, QUrl(QStringLiteral("https://example.test/mac-arm.dmg")));

    const auto macIntel = UpdateChecker::parseRelease(sampleRelease(), "macos", "x64");
    ASSERT_TRUE(macIntel);
    EXPECT_EQ(macIntel->assetUrl, QUrl(QStringLiteral("https://example.test/mac-x64.dmg")));

    // aarch64 в имени файла — та же архитектура, что arm64.
    const auto linuxArm = UpdateChecker::parseRelease(sampleRelease(), "linux", "arm64");
    ASSERT_TRUE(linuxArm);
    EXPECT_EQ(linuxArm->assetUrl, QUrl(QStringLiteral("https://example.test/linux-arm.AppImage")));
}

TEST(UpdateChecker, WrongArchitectureFallsBackToReleasePage)
{
    // Пакета для Windows arm64 нет, и подсовывать x64 нельзя: он не запустится.
    const auto release = UpdateChecker::parseRelease(sampleRelease(), "windows", "arm64");
    ASSERT_TRUE(release);
    EXPECT_TRUE(release->assetUrl.isEmpty());
    EXPECT_EQ(release->downloadUrl(), release->pageUrl);
}

TEST(UpdateChecker, ParseRejectsGarbage)
{
    QString error;
    EXPECT_FALSE(UpdateChecker::parseRelease("not json", "linux", "x64", &error));
    EXPECT_FALSE(error.isEmpty());

    error.clear();
    // Тег без числа не версия: сравнивать его не с чем.
    EXPECT_FALSE(UpdateChecker::parseRelease(sampleRelease(QStringLiteral("nightly")), "linux",
                                             "x64", &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(UpdateChecker, LongNotesAreTruncated)
{
    const QByteArray json = QByteArrayLiteral(R"({"tag_name":"v1.0.0","body":")")
                            + QByteArray(5000, 'x') + QByteArrayLiteral("\"}");
    const auto release = UpdateChecker::parseRelease(json, "linux", "x64");
    ASSERT_TRUE(release);
    EXPECT_LT(release->notes.size(), 2100);
}

namespace {

/// Проверка против файла вместо GitHub: сетевой слой Qt читает `file://` так же.
struct CheckOutcome
{
    int available = 0;
    int upToDate = 0;
    int failed = 0;
    QString version;
};

CheckOutcome runCheck(const QByteArray &reply, const QString &current)
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("latest.json"));
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(reply);
    file.close();

    UpdateChecker checker(nullptr, QUrl::fromLocalFile(path));
    // Счётчики вместо QSignalSpy: Qt6::Test в набор не подключён.
    CheckOutcome outcome;
    QObject::connect(&checker, &UpdateChecker::updateAvailable, &checker,
                     [&](const ReleaseInfo &release) {
                         ++outcome.available;
                         outcome.version = release.version;
                     });
    QObject::connect(&checker, &UpdateChecker::upToDate, &checker,
                     [&](const QString &) { ++outcome.upToDate; });
    QObject::connect(&checker, &UpdateChecker::failed, &checker,
                     [&](const QString &) { ++outcome.failed; });

    checker.check(current);
    EXPECT_TRUE(checker.isRunning());
    spotty::test::waitFor(
        [&] { return outcome.available + outcome.upToDate + outcome.failed > 0; });
    return outcome;
}

} // namespace

TEST(UpdateChecker, NewerReleaseIsOffered)
{
    const CheckOutcome outcome = runCheck(sampleRelease(), QStringLiteral("0.8.0"));
    EXPECT_EQ(outcome.available, 1);
    EXPECT_EQ(outcome.upToDate, 0);
    EXPECT_EQ(outcome.failed, 0);
    EXPECT_EQ(outcome.version, QStringLiteral("0.9.0"));
}

TEST(UpdateChecker, SameOrNewerLocalVersionIsUpToDate)
{
    const CheckOutcome same = runCheck(sampleRelease(), QStringLiteral("0.9.0"));
    EXPECT_EQ(same.upToDate, 1);
    EXPECT_EQ(same.available, 0);

    // Сборка из разработки новее последнего релиза — предлагать «обновиться» назад нельзя.
    const CheckOutcome ahead = runCheck(sampleRelease(), QStringLiteral("0.10.0"));
    EXPECT_EQ(ahead.upToDate, 1);
}

TEST(UpdateChecker, UnreadableReplyIsAFailureNotASilence)
{
    const CheckOutcome outcome = runCheck("<html>rate limited</html>", QStringLiteral("0.8.0"));
    EXPECT_EQ(outcome.failed, 1);
    EXPECT_EQ(outcome.available, 0);
}
