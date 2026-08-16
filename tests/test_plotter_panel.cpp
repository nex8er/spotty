/**
 * \file test_plotter_panel.cpp
 * \brief Тесты проводки панели плоттера: профиль, таблица, ширины колонок.
 */
#include "PlotterPanel.h"

#include <spotty/data/PlotModel.h>
#include <spotty/data/PlotProfile.h>
#include <spotty/data/PlotViewState.h>

#include "support/FakePanelHost.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QSplitter>

using namespace spotty;

namespace {

class Panel : public ::testing::Test
{
protected:
    void SetUp() override
    {
        host = std::make_unique<test::FakePanelHost>(dir.path());
        model = std::make_unique<PlotModel>();
        view = std::make_unique<PlotViewState>();
    }

    /// \brief Создать панель поверх нынешних модели и вида.
    void build()
    {
        panel = std::make_unique<PlotterPanel>(host.get(), model.get(), view.get());
        panel->resize(320, 500);
        panel->show();
        QApplication::processEvents();
    }

    /// \brief Записать профиль на диск и выбрать его в настройках хоста.
    void storeProfile(const QString &name, const QList<quint32> &colours)
    {
        PlotProfile profile;
        profile.name = name;
        profile.separator = QStringLiteral(",");
        profile.capacity = 4321;
        for (const quint32 colour : colours) {
            PlotProfileSeries series;
            series.name = QStringLiteral("named%1").arg(profile.series.size());
            series.nameIsCustom = true;
            series.color = colour;
            profile.series.append(series);
        }

        PlotProfileStore store(dir.path());
        ASSERT_TRUE(store.save(profile));
        host->settings.insert(QStringLiteral("profile"), name);
    }

    test::TempDir dir;
    std::unique_ptr<test::FakePanelHost> host;
    std::unique_ptr<PlotModel> model;
    std::unique_ptr<PlotViewState> view;
    std::unique_ptr<PlotterPanel> panel;
};

} // namespace

TEST_F(Panel, ProfileIsAppliedToSeriesThatArriveAfterStartup)
{
    // Та самая ошибка: профиль читается при запуске, когда рядов ещё ноль, и цикл его
    // применения не выполняется ни разу. Ряды появляются с первой строкой устройства, и
    // без повторного наложения они получают цвета по умолчанию, а пользователь видит, что
    // «настройки не сохранились».
    storeProfile(QStringLiteral("board"), {0xFF112233, 0xFF445566});
    build();

    ASSERT_EQ(model->seriesCount(), 0);

    model->feed(QStringLiteral("1,2"), 0);
    QApplication::processEvents();

    ASSERT_EQ(model->seriesCount(), 2);
    EXPECT_EQ(model->series(0).color, 0xFF112233u);
    EXPECT_EQ(model->series(1).color, 0xFF445566u);
    EXPECT_EQ(model->series(0).name, QStringLiteral("named0"));
}

TEST_F(Panel, ProfileReachesColumnsThatAppearOneByOne)
{
    // Колонки приходят по одной, и профиль обязан догонять каждую: иначе шестая колонка
    // семиколоночного потока осталась бы с цветом по умолчанию.
    storeProfile(QStringLiteral("wide"), {0xFF010101, 0xFF020202, 0xFF030303});
    build();

    model->feed(QStringLiteral("1"), 0);
    QApplication::processEvents();
    EXPECT_EQ(model->series(0).color, 0xFF010101u);

    model->feed(QStringLiteral("1,2,3"), 1);
    QApplication::processEvents();

    ASSERT_EQ(model->seriesCount(), 3);
    EXPECT_EQ(model->series(2).color, 0xFF030303u);
}

TEST_F(Panel, ProfileCarriesNonSeriesSettingsToo)
{
    storeProfile(QStringLiteral("board"), {0xFF112233});
    build();
    QApplication::processEvents();

    EXPECT_EQ(model->capacity(), 4321);
}

TEST_F(Panel, ChangingAColourRewritesTheProfileFile)
{
    storeProfile(QStringLiteral("board"), {0xFF112233});
    build();
    model->feed(QStringLiteral("1"), 0);
    QApplication::processEvents();

    model->setSeriesColor(0, 0xFFAABBCC);

    // Запись отложена, чтобы не дёргать диск на каждое нажатие.
    ASSERT_TRUE(test::waitFor([this] {
        return PlotProfileStore(dir.path()).load(QStringLiteral("board")).series.value(0).color
               == 0xFFAABBCCu;
    }, 3000));
}

TEST_F(Panel, IncomingSamplesDoNotRewriteTheProfile)
{
    // Сдвиг окна за данными приходит на каждый отсчёт. Сохранять по нему значило бы писать
    // файл несколько раз в секунду всё время, пока идёт поток.
    storeProfile(QStringLiteral("board"), {0xFF112233});
    build();
    model->feed(QStringLiteral("1"), 0);
    QApplication::processEvents();

    const QFileInfo file(dir.filePath(QStringLiteral("board.json")));
    const QDateTime before = file.lastModified();

    for (int i = 1; i < 200; ++i)
        model->feed(QString::number(i), qint64(i) * 1'000'000);
    QApplication::processEvents();

    EXPECT_EQ(QFileInfo(dir.filePath(QStringLiteral("board.json"))).lastModified(), before);
}

TEST_F(Panel, SplitterPositionSurvivesRebuild)
{
    // Высоту миниатюры подбирают под задачу один раз; возвращать её к исходной каждый
    // сеанс значило бы отменять этот выбор.
    build();
    auto *splitter = panel->findChild<QSplitter *>();
    ASSERT_NE(splitter, nullptr);

    splitter->setSizes({300, 200});
    Q_EMIT splitter->splitterMoved(300, 1);
    QApplication::processEvents();

    const QList<int> saved = splitter->sizes();
    ASSERT_FALSE(host->settings.value(QStringLiteral("panelSplitter")).toByteArray().isEmpty());

    // Пересоздаём панель поверх тех же настроек — как при следующем запуске.
    panel.reset();
    build();

    auto *restored = panel->findChild<QSplitter *>();
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->sizes(), saved);
}

TEST_F(Panel, MiniatureAndTableBothStayUsable)
{
    // Схлопывать нечего: панель без графика или без таблицы бесполезна, а вернуть
    // схлопнутое можно было бы только попав в захват шириной в пять пикселей.
    build();
    auto *splitter = panel->findChild<QSplitter *>();
    ASSERT_NE(splitter, nullptr);

    EXPECT_FALSE(splitter->childrenCollapsible());
    EXPECT_EQ(splitter->orientation(), Qt::Vertical);
    EXPECT_EQ(splitter->count(), 2);

    // Имя — это то, чем правило «#plotterSplitter::handle» из таблицы стилей находит
    // разделитель. Переименование не сломает ни сборку, ни тест поведения: линия просто
    // молча исчезнет, и захват снова станет ненаходимым.
    EXPECT_EQ(splitter->objectName(), QStringLiteral("plotterSplitter"));

    // Ширина захвата и отбивка в таблице стилей связаны числами: 1 + 1 + 1. Разъедутся —
    // и линия либо исчезнет, либо превратится в плашку, причём молча.
    EXPECT_EQ(splitter->handleWidth(), 2);
}

TEST_F(Panel, WithoutAProfileNothingIsWritten)
{
    build();
    model->feed(QStringLiteral("1,2"), 0);
    model->setSeriesColor(0, 0xFFAABBCC);
    QApplication::processEvents();

    EXPECT_TRUE(PlotProfileStore(dir.path()).profiles().isEmpty());
}
