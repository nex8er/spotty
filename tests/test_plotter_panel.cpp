/**
 * \file test_plotter_panel.cpp
 * \brief Тесты проводки панели плоттера: профиль, таблица, ширины колонок.
 */
#include "PlotterPanel.h"
#include "PlotterPlugin.h"

#include <spotty/data/PlotModel.h>
#include <spotty/data/PlotProfile.h>
#include <spotty/data/PlotViewState.h>

#include "support/FakePanelHost.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>

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

    // Старые профили могли хранить точное число отсчётов. В новом поле оно показывается
    // тысячами, поэтому ближайшее представимое значение — 4K.
    EXPECT_EQ(model->capacity(), 4000);
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

TEST_F(Panel, MiniatureHeightSurvivesRebuild)
{
    build();
    auto *splitter = panel->findChild<QSplitter *>();
    ASSERT_NE(splitter, nullptr);

    splitter->setSizes({240, 260});
    Q_EMIT splitter->splitterMoved(240, 1);
    QApplication::processEvents();

    const int savedHeight = splitter->sizes().first();
    EXPECT_EQ(host->settings.value(QStringLiteral("miniatureHeight")).toInt(), savedHeight);

    panel.reset();
    build();

    auto *restored = panel->findChild<QSplitter *>();
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->sizes().first(), savedHeight);
}

TEST_F(Panel, ModeComboKeepsIconsAndChangesTheView)
{
    build();
    auto *mode = panel->findChild<QComboBox *>(QStringLiteral("plotMode"));
    ASSERT_NE(mode, nullptr);
    ASSERT_EQ(mode->count(), 6);
    EXPECT_EQ(mode->itemText(0), QStringLiteral("General"));
    for (int index = 0; index < mode->count(); ++index)
        EXPECT_FALSE(mode->itemIcon(index).isNull());
    EXPECT_TRUE(host->iconGlyphs.contains(char32_t(0xF0100)));
    EXPECT_TRUE(host->iconGlyphs.contains(char32_t(0xF04EB)));

    const int spectrum = mode->findData(int(PlotViewState::Mode::Spectrum));
    ASSERT_GE(spectrum, 0);
    mode->setCurrentIndex(spectrum);
    EXPECT_EQ(view->mode(), PlotViewState::Mode::Spectrum);

    view->setMode(PlotViewState::Mode::Xy);
    EXPECT_EQ(mode->currentData().toInt(), int(PlotViewState::Mode::Xy));
}

TEST_F(Panel, ProfileAppliesAndSavesPlotScale)
{
    storeProfile(QStringLiteral("board"), {0xFF112233});
    PlotProfileStore store(dir.path());
    PlotProfile profile = store.load(QStringLiteral("board"));
    profile.horizontalDurationNs = 30'000'000'000LL;
    profile.verticalZoom = 2.5;
    profile.verticalOffset = -0.75;
    ASSERT_TRUE(store.save(profile));

    build();
    EXPECT_EQ(view->windowDuration(), 30'000'000'000LL);
    EXPECT_DOUBLE_EQ(view->verticalZoom(), 2.5);
    EXPECT_DOUBLE_EQ(view->verticalOffset(), -0.75);

    view->setWindowDuration(20'000'000'000LL);
    view->setVerticalTransform(1.5, 0.25);

    ASSERT_TRUE(test::waitFor([this] {
        const PlotProfile saved = PlotProfileStore(dir.path()).load(QStringLiteral("board"));
        return saved.horizontalDurationNs == 20'000'000'000LL
               && qFuzzyCompare(saved.verticalZoom, 1.5)
               && qFuzzyCompare(saved.verticalOffset, 0.25);
    }, 3000));
}

TEST_F(Panel, BufferSizeIsShownAndStoredInThousands)
{
    build();
    auto *buffer = panel->findChild<QSpinBox *>(QStringLiteral("plotBuffer"));
    ASSERT_NE(buffer, nullptr);

    EXPECT_EQ(buffer->minimum(), 1);
    EXPECT_EQ(buffer->maximum(), 100);
    EXPECT_EQ(buffer->value(), 50);
    EXPECT_EQ(buffer->suffix(), QStringLiteral("K"));

    buffer->setValue(42);
    EXPECT_EQ(model->capacity(), 42'000);
    EXPECT_EQ(host->settings.value(QStringLiteral("capacity")).toInt(), 42'000);
    EXPECT_EQ(host->settings.value(QStringLiteral("bufferK")).toInt(), 42);
}

TEST_F(Panel, ReadsBufferAndSeparatorFromGlobalSettings)
{
    host->settings.insert(QStringLiteral("bufferK"), 12);
    host->settings.insert(QStringLiteral("separator"), QStringLiteral(";"));
    build();

    auto *buffer = panel->findChild<QSpinBox *>(QStringLiteral("plotBuffer"));
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->value(), 12);
    EXPECT_EQ(model->capacity(), 12'000);
    EXPECT_EQ(model->separator(), u';');
}

TEST_F(Panel, ShowsDeltaColumnWhenEnabledInSettings)
{
    host->settings.insert(QStringLiteral("showDelta"), true);
    build();
    model->feed(QStringLiteral("2"), 0);
    model->feed(QStringLiteral("7"), 1);

    auto *table = panel->findChild<QTableWidget *>();
    ASSERT_NE(table, nullptr);
    EXPECT_FALSE(table->isColumnHidden(5));
    EXPECT_EQ(table->horizontalHeaderItem(5)->text(), QStringLiteral("Delta"));
    ASSERT_TRUE(test::waitFor([table] { return table->item(0, 5)->text() == QStringLiteral("5"); },
                              3000));
}

TEST(PlotterPlugin, ProvidesSettingsForTheGlobalDataFormat)
{
    PlotterPlugin plugin;
    const SettingsSchema schema = plugin.settingsSchema();
    const SettingsField *separator = schema.field(QStringLiteral("separator"));
    const SettingsField *buffer = schema.field(QStringLiteral("bufferK"));
    const SettingsField *delta = schema.field(QStringLiteral("showDelta"));
    ASSERT_NE(separator, nullptr);
    ASSERT_NE(buffer, nullptr);
    ASSERT_NE(delta, nullptr);

    EXPECT_EQ(separator->type, SettingsField::Text);
    EXPECT_EQ(separator->defaultValue.toString(), QStringLiteral(","));
    EXPECT_EQ(buffer->type, SettingsField::Integer);
    EXPECT_EQ(buffer->defaultValue.toInt(), 50);
    EXPECT_EQ(buffer->minimum, 1);
    EXPECT_EQ(buffer->maximum, 100);
    EXPECT_EQ(buffer->suffix, QStringLiteral("K"));
    EXPECT_EQ(delta->type, SettingsField::Toggle);
    EXPECT_FALSE(delta->defaultValue.toBool());
}

TEST_F(Panel, ResetProfileRestoresReportedFieldNamesAndDefaults)
{
    storeProfile(QStringLiteral("board"), {0xFF112233, 0xFF445566});
    build();
    model->feed(QStringLiteral("1,2"), 0);
    model->feed(QStringLiteral("voltage,current"), 1);
    model->setSeriesName(0, QStringLiteral("battery"));
    // Оба от умолчания: по умолчанию виден только индекс 0 — здесь наоборот.
    model->setSeriesVisible(0, false);
    model->setSeriesVisible(1, true);
    model->setSeriesRange(0, true, -5.0, 5.0);
    model->setSeparator(u';');
    model->setCapacity(100);
    model->setXAxisSeries(0);
    view->setMode(PlotViewState::Mode::Spectrum);

    auto *reset = panel->findChild<QToolButton *>(QStringLiteral("resetPlotProfile"));
    ASSERT_NE(reset, nullptr);
    ASSERT_TRUE(reset->isEnabled());
    EXPECT_TRUE(host->iconGlyphs.contains(char32_t(0xF006F)));
    reset->click();

    EXPECT_EQ(model->separator(), u',');
    EXPECT_EQ(model->capacity(), 50000);
    EXPECT_EQ(model->xAxisSeries(), -1);
    EXPECT_EQ(view->mode(), PlotViewState::Mode::TimeSeries);
    EXPECT_EQ(view->windowDuration(), PlotViewState::kDefaultDuration);
    EXPECT_DOUBLE_EQ(view->verticalZoom(), 1.0);
    EXPECT_DOUBLE_EQ(view->verticalOffset(), 0.0);
    EXPECT_EQ(model->series(0).name, QStringLiteral("voltage"));
    EXPECT_EQ(model->series(1).name, QStringLiteral("current"));
    EXPECT_FALSE(model->series(0).nameIsCustom);
    // Умолчание — виден только первый ряд.
    EXPECT_TRUE(model->series(0).visible);
    EXPECT_FALSE(model->series(1).visible);
    EXPECT_FALSE(model->series(0).hasCustomRange);

    const PlotProfile stored = PlotProfileStore(dir.path()).load(QStringLiteral("board"));
    ASSERT_EQ(stored.series.size(), 2);
    EXPECT_FALSE(stored.series.at(0).nameIsCustom);
    EXPECT_EQ(stored.series.at(0).name, QStringLiteral("voltage"));
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

TEST_F(Panel, PauseDiscardsIncomingPlotSamples)
{
    PlotterPlugin plugin;
    std::unique_ptr<QWidget> created(plugin.createPanel(QStringLiteral("plotter"), host.get(),
                                                         nullptr));
    auto *pluginModel = plugin.findChild<PlotModel *>();
    auto *pluginView = plugin.findChild<PlotViewState *>();
    ASSERT_NE(pluginModel, nullptr);
    ASSERT_NE(pluginView, nullptr);

    TerminalLine first;
    first.text = QStringLiteral("1,2");
    first.complete = true;
    first.direction = DataDirection::Rx;
    host->appendTerminalLine(first);
    ASSERT_EQ(pluginModel->samples().sampleCount(), 1);

    pluginView->setPaused(true);
    TerminalLine paused;
    paused.text = QStringLiteral("3,4");
    paused.complete = true;
    paused.direction = DataDirection::Rx;
    host->appendTerminalLine(paused);
    EXPECT_EQ(pluginModel->samples().sampleCount(), 1);

    pluginView->setPaused(false);
    TerminalLine resumed;
    resumed.text = QStringLiteral("5,6");
    resumed.complete = true;
    resumed.direction = DataDirection::Rx;
    host->appendTerminalLine(resumed);

    ASSERT_EQ(pluginModel->samples().sampleCount(), 2);
    EXPECT_DOUBLE_EQ(pluginModel->samples().at(1, 0), 5.0);
    EXPECT_DOUBLE_EQ(pluginModel->samples().at(1, 1), 6.0);
}
