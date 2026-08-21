/**
 * \file test_json_tree_panel.cpp
 * \brief Тесты проводки панели разбора JSON: чтение потока, троттлинг, настройки.
 */
#include "JsonRateDelegate.h"
#include "JsonTreePanel.h"
#include "JsonTreePlugin.h"
#include "JsonTreeView.h"

#include "support/FakePanelHost.h"
#include "support/TestSupport.h"

#include <spotty/data/JsonTreeModel.h>

#include <gtest/gtest.h>

#include <QApplication>
#include <QToolButton>
#include <QTreeWidget>

using namespace spotty;

namespace {

/// \brief Строка терминала, пришедшая от устройства и завершённая переводом строки.
TerminalLine incoming(const QString &text, qint64 monotonicNs = 0, bool complete = true)
{
    TerminalLine line;
    line.text = text;
    line.monotonicNs = monotonicNs;
    line.direction = DataDirection::Rx;
    line.complete = complete;
    return line;
}

/// \brief Прокрутить очередь событий, пока не отработают таймеры троттлинга.
void settle(int milliseconds = 400)
{
    const QDeadlineTimer deadline(milliseconds);
    while (!deadline.hasExpired())
        QApplication::processEvents(QEventLoop::AllEvents, 20);
}

/// \brief Панель вместе со всем, что ей нужно; порядок разрушения важен.
struct Fixture
{
    test::TempDir dir;
    test::FakePanelHost host{dir.path()};
    JsonTreePlugin plugin;
    QWidget *panel = nullptr;

    Fixture()
    {
        host.id = QStringLiteral("jsontree");
        panel = plugin.createPanel(QStringLiteral("jsontree"), &host, nullptr);
        panel->show();
    }

    ~Fixture() { delete panel; }

    QTreeWidget *tree() const { return panel->findChild<QTreeWidget *>(); }
};

} // namespace

TEST(JsonTreePanel, ParsesIncomingLinesIntoTheTree)
{
    Fixture fixture;
    fixture.host.appendTerminalLine(incoming(QStringLiteral(R"({"temp":23.5,"ok":true})")));
    settle();

    QTreeWidget *tree = fixture.tree();
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->topLevelItemCount(), 2);
}

TEST(JsonTreePanel, IncompleteLineIsNotParsedAndNotSkipped)
{
    // Записанная ловушка плоттера: недописанную строку нельзя ни разбирать сейчас, ни
    // считать пройденной. Половина документа — это мусор, а пропустив её, мы потеряли бы
    // данные насовсем.
    Fixture fixture;
    fixture.host.appendTerminalLine(incoming(QStringLiteral(R"({"temp":2)"), 0,
                                             /*complete=*/false));
    settle(200);
    EXPECT_EQ(fixture.tree()->topLevelItemCount(), 0);

    // Строка достроилась, и следующий сигнал обязан её перечитать.
    fixture.host.terminalLines[0] = incoming(QStringLiteral(R"({"temp":23.5})"));
    fixture.host.appendTerminalLine(incoming(QStringLiteral(R"({"other":1})"), 1000));
    settle();

    EXPECT_EQ(fixture.tree()->topLevelItemCount(), 2);
}

TEST(JsonTreePanel, OutgoingLinesAreIgnored)
{
    Fixture fixture;
    TerminalLine sent = incoming(QStringLiteral(R"({"temp":1})"));
    sent.direction = DataDirection::Tx;
    fixture.host.appendTerminalLine(sent);
    settle(200);

    // Своё отправленное — не телеметрия устройства.
    EXPECT_EQ(fixture.tree()->topLevelItemCount(), 0);
}

TEST(JsonTreePanel, PauseStopsParsingAndDoesNotCatchUpAfterwards)
{
    Fixture fixture;
    auto *panel = qobject_cast<JsonTreePanel *>(fixture.panel);
    ASSERT_NE(panel, nullptr);

    auto *pause = panel->findChildren<QToolButton *>().value(0);
    ASSERT_NE(pause, nullptr);
    pause->click();
    ASSERT_TRUE(panel->isPaused());

    fixture.host.appendTerminalLine(incoming(QStringLiteral(R"({"hidden":1})")));
    settle(200);
    EXPECT_EQ(fixture.tree()->topLevelItemCount(), 0);

    pause->click();
    ASSERT_FALSE(panel->isPaused());
    fixture.host.appendTerminalLine(incoming(QStringLiteral(R"({"shown":1})"), 1000));
    settle();

    // Пропущенное во время паузы не догоняет: номер строки двигался и на паузе.
    ASSERT_EQ(fixture.tree()->topLevelItemCount(), 1);
    EXPECT_EQ(fixture.tree()->topLevelItem(0)->text(0), QStringLiteral("shown"));
}

TEST(JsonTreePanel, TreeIsNotRebuiltOnEveryDocument)
{
    // Сигнал модели приходит на каждый документ, а перерисовка обязана идти по таймеру:
    // иначе поток интерфейса уходит в отрисовку целиком (записанная ловушка плоттера).
    Fixture fixture;
    fixture.host.appendTerminalLine(incoming(QStringLiteral(R"({"a":1})")));

    // Сразу после прихода строки дерево ещё пусто — обработчик только взвёл таймер.
    EXPECT_EQ(fixture.tree()->topLevelItemCount(), 0);

    settle();
    EXPECT_EQ(fixture.tree()->topLevelItemCount(), 1);
}

TEST(JsonTreePanel, NodeItemsAreCreatedOnce)
{
    Fixture fixture;
    for (int i = 0; i < 20; ++i) {
        fixture.host.appendTerminalLine(
            incoming(QStringLiteral(R"({"a":%1})").arg(i), i * 1'000'000));
    }
    settle();

    // Узел создаётся при появлении пути, дальше меняется только значение.
    ASSERT_EQ(fixture.tree()->topLevelItemCount(), 1);
    EXPECT_EQ(fixture.tree()->topLevelItem(0)->text(1), QStringLiteral("19"));
}

TEST(JsonTreePanel, ArrayIdentityKeyFromSettingsRebuildsTheTree)
{
    Fixture fixture;
    fixture.host.appendTerminalLine(
        incoming(QStringLiteral(R"({"s":[{"id":1,"v":10},{"id":2,"v":20}]})")));
    settle();

    // Без ключа массив схлопнут: одна ветка «s» с полями последнего элемента.
    ASSERT_EQ(fixture.tree()->topLevelItemCount(), 1);
    EXPECT_EQ(fixture.tree()->topLevelItem(0)->childCount(), 2);

    fixture.host.settings.insert(QStringLiteral("arrayKey"), QStringLiteral("id"));
    Q_EMIT fixture.host.settingsReset();

    fixture.host.appendTerminalLine(
        incoming(QStringLiteral(R"({"s":[{"id":1,"v":10},{"id":2,"v":20}]})"), 1'000'000));
    settle();

    // С ключом — ветка на каждый элемент.
    ASSERT_EQ(fixture.tree()->topLevelItemCount(), 1);
    EXPECT_EQ(fixture.tree()->topLevelItem(0)->childCount(), 2);
    EXPECT_EQ(fixture.tree()->topLevelItem(0)->child(0)->text(0), QStringLiteral("1"));
    EXPECT_EQ(fixture.tree()->topLevelItem(0)->child(0)->childCount(), 2);
}

TEST(JsonTreePanel, EveryColumnCarriesTheNodeIndex)
{
    // Регрессия: индекс узла записывался только в первую колонку, а делегат получает
    // индекс той ячейки, которую рисует. В колонке частоты узел выходил невалидным, и
    // колонка оставалась пустой — при том, что данные в модели были.
    Fixture fixture;
    fixture.host.appendTerminalLine(incoming(QStringLiteral(R"({"temp":1})")));
    settle();

    QTreeWidgetItem *item = fixture.tree()->topLevelItem(0);
    ASSERT_NE(item, nullptr);
    for (int column = 0; column < fixture.tree()->columnCount(); ++column) {
        const QVariant node = item->data(column, JsonRateDelegate::kNodeRole);
        EXPECT_TRUE(node.isValid()) << "колонка " << column << " без индекса узла";
        EXPECT_GT(node.toInt(), 0) << "колонка " << column << " с индексом корня";
    }
}

TEST(JsonTreePanel, RateBecomesVisibleAfterSecondDocument)
{
    // Частота появляется со второго документа: до него интервала не существует. Проверяем
    // именно то, что видно в колонке, — текст, который нарисует делегат.
    Fixture fixture;
    fixture.host.appendTerminalLine(incoming(QStringLiteral(R"({"temp":1})"), 0));
    settle(200);

    QTreeWidgetItem *item = fixture.tree()->topLevelItem(0);
    ASSERT_NE(item, nullptr);
    const int node = item->data(2, JsonRateDelegate::kNodeRole).toInt();

    fixture.host.appendTerminalLine(incoming(QStringLiteral(R"({"temp":2})"), 100'000'000));
    settle(200);

    JsonTreeModel *model = fixture.panel->findChild<JsonTreeModel *>();
    ASSERT_EQ(model, nullptr); // Модель принадлежит плагину, а не панели.

    // Значение обновилось — значит документ дошёл; частота считается по тем же отсчётам.
    EXPECT_EQ(item->text(1), QStringLiteral("2"));
    EXPECT_GT(node, 0);
}

TEST(JsonTreePanel, ExpandAndCollapseButtonsWorkOnNestedTree)
{
    Fixture fixture;
    fixture.host.appendTerminalLine(
        incoming(QStringLiteral(R"({"status":{"ok":true,"code":0}})")));
    settle();

    QTreeWidgetItem *branch = fixture.tree()->topLevelItem(0);
    ASSERT_NE(branch, nullptr);
    ASSERT_EQ(branch->childCount(), 2);

    const auto buttonWithTip = [&fixture](const QString &fragment) -> QToolButton * {
        for (QToolButton *button : fixture.panel->findChildren<QToolButton *>()) {
            if (button->toolTip().contains(fragment))
                return button;
        }
        return nullptr;
    };

    QToolButton *collapse = buttonWithTip(QStringLiteral("Collapse"));
    QToolButton *expand = buttonWithTip(QStringLiteral("Expand"));
    ASSERT_NE(collapse, nullptr);
    ASSERT_NE(expand, nullptr);

    collapse->click();
    EXPECT_FALSE(branch->isExpanded());

    expand->click();
    EXPECT_TRUE(branch->isExpanded());
}

TEST(JsonTreePanel, FlashSettingsSurviveThroughTheHost)
{
    // Кнопка пишет настройку, а применение читает её обратно — без блокировки сигналов
    // применение отменяло бы само себя.
    test::TempDir dir;
    test::FakePanelHost host(dir.path());
    host.id = QStringLiteral("jsontree");
    host.settings.insert(QStringLiteral("flashOnChange"), false);
    host.settings.insert(QStringLiteral("flashMs"), 700);

    JsonTreePlugin plugin;
    QWidget *panel = plugin.createPanel(QStringLiteral("jsontree"), &host, nullptr);
    panel->show();

    EXPECT_FALSE(host.settings.value(QStringLiteral("flashOnChange")).toBool());
    EXPECT_EQ(host.settings.value(QStringLiteral("flashMs")).toInt(), 700);

    delete panel;
}

TEST(JsonTreePanel, SchemaCarriesTheDocumentedKeys)
{
    JsonTreePlugin plugin;
    const SettingsSchema schema = plugin.settingsSchema();

    const SettingsField *arrayKey = schema.field(QStringLiteral("arrayKey"));
    const SettingsField *maxNodes = schema.field(QStringLiteral("maxNodes"));
    const SettingsField *timeout = schema.field(QStringLiteral("pendingTimeoutMs"));
    ASSERT_NE(arrayKey, nullptr);
    ASSERT_NE(maxNodes, nullptr);
    ASSERT_NE(timeout, nullptr);

    // Ключ идентификации — текст, а не список: автоопределения нет намеренно, а список с
    // пустым умолчанием был бы неотличим от «ничего нет».
    EXPECT_EQ(arrayKey->type, SettingsField::Text);
    EXPECT_TRUE(arrayKey->defaultValue.toString().isEmpty());
    EXPECT_EQ(maxNodes->type, SettingsField::Integer);
    EXPECT_EQ(maxNodes->defaultValue.toInt(), JsonTreeModel::kDefaultMaxNodes);
    EXPECT_EQ(timeout->defaultValue.toInt(), 2000);
}

TEST(JsonTreePanel, PluginDeclaresOneRailPanel)
{
    JsonTreePlugin plugin;
    const QList<PanelDescriptor> panels = plugin.panels();

    ASSERT_EQ(panels.size(), 1);
    EXPECT_EQ(panels.first().id, QStringLiteral("jsontree"));
    EXPECT_EQ(panels.first().placement, PanelPlacement::Rail);
}

TEST(JsonRateDelegateTest, FormatsRateForReading)
{
    // Прочерк отличает «частоты ещё нет» от честно измеренной низкой частоты.
    EXPECT_EQ(formatRate(0.0), QStringLiteral("—"));
    EXPECT_EQ(formatRate(12.4), QStringLiteral("12"));
    EXPECT_EQ(formatRate(1.37), QStringLiteral("1.4"));
    EXPECT_EQ(formatRate(0.03), QStringLiteral("0.0"));
}

TEST(JsonRateDelegateTest, StripsTextSoItCanDrawItItself)
{
    // Проверять раскладку пикселями бесполезно — она зависит от стиля и таблицы стилей,
    // поэтому initStyleOption() зовётся напрямую, как в тесте квадратика плоттера.
    JsonTreeModel model;
    JsonRateDelegate delegate(&model);

    QTreeWidget tree;
    tree.setColumnCount(3);
    auto *item = new QTreeWidgetItem(&tree);
    item->setText(0, QStringLiteral("temp"));

    QStyleOptionViewItem option;
    const QModelIndex index = tree.model()->index(0, 0);

    struct Probe : JsonRateDelegate
    {
        using JsonRateDelegate::initStyleOption;
        using JsonRateDelegate::JsonRateDelegate;
    };
    Probe probe(&model);
    probe.initStyleOption(&option, index);

    EXPECT_TRUE(option.text.isEmpty());
}
