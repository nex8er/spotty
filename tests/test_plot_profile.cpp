/**
 * \file test_plot_profile.cpp
 * \brief Тесты профилей плоттера: круговой обход через файл и автоподбор.
 */
#include <spotty/data/PlotProfile.h>

#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <QFile>

using namespace spotty;

namespace {

class Profiles : public ::testing::Test
{
protected:
    test::TempDir dir;

    PlotProfileStore store() { return PlotProfileStore(dir.path()); }

    /// \brief Профиль на две колонки с заданными именами.
    static PlotProfile make(const QString &name, const QStringList &columns)
    {
        PlotProfile profile;
        profile.name = name;
        for (const QString &column : columns) {
            PlotProfileSeries series;
            series.name = column;
            profile.series.append(series);
        }
        return profile;
    }
};

} // namespace

TEST_F(Profiles, RoundTripsEveryField)
{
    PlotProfile profile = make(QStringLiteral("board"), {QStringLiteral("volt"),
                                                          QStringLiteral("curr")});
    profile.separator = QStringLiteral(";");
    profile.xAxis = 1;
    profile.capacity = 12345;
    profile.mode = QStringLiteral("spectrum");
    profile.series[0].color = 0xFF102030;
    profile.series[0].nameIsCustom = true;
    profile.series[1].visible = false;
    profile.series[1].hasCustomRange = true;
    profile.series[1].customMinimum = -3.5;
    profile.series[1].customMaximum = 7.25;

    ASSERT_TRUE(store().save(profile));

    const PlotProfile loaded = store().load(QStringLiteral("board"));

    EXPECT_EQ(loaded.separator, QStringLiteral(";"));
    EXPECT_EQ(loaded.xAxis, 1);
    EXPECT_EQ(loaded.capacity, 12345);
    EXPECT_EQ(loaded.mode, QStringLiteral("spectrum"));
    ASSERT_EQ(loaded.series.size(), 2);
    EXPECT_EQ(loaded.series.at(0).color, 0xFF102030u);
    EXPECT_TRUE(loaded.series.at(0).nameIsCustom);
    EXPECT_FALSE(loaded.series.at(1).visible);
    EXPECT_TRUE(loaded.series.at(1).hasCustomRange);
    EXPECT_DOUBLE_EQ(loaded.series.at(1).customMinimum, -3.5);
    EXPECT_DOUBLE_EQ(loaded.series.at(1).customMaximum, 7.25);
}

TEST_F(Profiles, SeriesWithoutLimitsStaysAutomatic)
{
    PlotProfile profile = make(QStringLiteral("plain"), {QStringLiteral("a")});
    ASSERT_TRUE(store().save(profile));

    EXPECT_FALSE(store().load(QStringLiteral("plain")).series.at(0).hasCustomRange);
}

TEST_F(Profiles, InvalidNameIsRefused)
{
    // Имя приходит от пользователя и не должно позволять писать за пределы каталога.
    EXPECT_FALSE(PlotProfileStore::isValidName(QStringLiteral("../escape")));
    EXPECT_FALSE(PlotProfileStore::isValidName(QStringLiteral(".hidden")));
    EXPECT_FALSE(PlotProfileStore::isValidName(QStringLiteral("a/b")));
    EXPECT_FALSE(PlotProfileStore::isValidName(QString()));
    EXPECT_TRUE(PlotProfileStore::isValidName(QStringLiteral("board 3")));

    EXPECT_FALSE(store().save(make(QStringLiteral("../escape"), {QStringLiteral("a")})));
}

TEST_F(Profiles, ProfilesAreListedByName)
{
    store().save(make(QStringLiteral("zulu"), {QStringLiteral("a")}));
    store().save(make(QStringLiteral("alpha"), {QStringLiteral("a")}));

    EXPECT_EQ(store().profiles(),
              QStringList({QStringLiteral("alpha"), QStringLiteral("zulu")}));
}

TEST_F(Profiles, DeleteRemovesTheFile)
{
    store().save(make(QStringLiteral("gone"), {QStringLiteral("a")}));
    ASSERT_EQ(store().profiles().size(), 1);

    EXPECT_TRUE(store().remove(QStringLiteral("gone")));
    EXPECT_TRUE(store().profiles().isEmpty());
}

TEST_F(Profiles, MalformedJsonIsNotOverwritten)
{
    // Пользователь мог править файл руками; молча стереть правку хуже, чем отказать.
    const QString path = dir.filePath(QStringLiteral("broken.json"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("{ это не json ");
    file.close();

    const PlotProfile loaded = store().load(QStringLiteral("broken"));

    EXPECT_TRUE(loaded.name.isEmpty());
    EXPECT_TRUE(QFile::exists(path));
}

TEST_F(Profiles, MatchNeedsTheSameColumnCount)
{
    // Профиль на две колонки не описывает поток из шести.
    const PlotProfile profile = make(QStringLiteral("two"), {QStringLiteral("a"),
                                                              QStringLiteral("b")});
    EXPECT_EQ(profile.matchScore(6, {}), 0);
    EXPECT_GT(profile.matchScore(2, {}), 0);
}

TEST_F(Profiles, MatchPrefersMatchingNames)
{
    store().save(make(QStringLiteral("guessed"), {QStringLiteral("alpha"),
                                                   QStringLiteral("bravo")}));
    store().save(make(QStringLiteral("named"), {QStringLiteral("volt"),
                                                 QStringLiteral("curr")}));

    const QString best =
        store().bestMatch(2, {QStringLiteral("volt"), QStringLiteral("curr")});

    EXPECT_EQ(best, QStringLiteral("named"));
}

TEST_F(Profiles, TieBreaksByMostRecentUse)
{
    // Оба подходят по числу колонок и ничем не отличаются по именам — значит, человек
    // почти наверняка хочет тот, с которым работал недавно.
    PlotProfile older = make(QStringLiteral("older"), {QStringLiteral("a")});
    older.lastUsed = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    PlotProfile newer = make(QStringLiteral("newer"), {QStringLiteral("a")});
    newer.lastUsed = QDateTime::fromString(QStringLiteral("2026-08-01T00:00:00Z"), Qt::ISODate);

    store().save(older);
    store().save(newer);

    EXPECT_EQ(store().bestMatch(1, {}), QStringLiteral("newer"));
}

TEST_F(Profiles, UnknownLayoutMatchesNothing)
{
    store().save(make(QStringLiteral("two"), {QStringLiteral("a"), QStringLiteral("b")}));

    EXPECT_TRUE(store().bestMatch(9, {}).isEmpty());
}

TEST_F(Profiles, MissingProfileLoadsEmpty)
{
    EXPECT_TRUE(store().load(QStringLiteral("nope")).name.isEmpty());
}
