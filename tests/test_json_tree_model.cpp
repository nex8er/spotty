/**
 * \file test_json_tree_model.cpp
 * \brief Тесты дерева путей JSON: раскладка, частота, пределы, уборка.
 */
#include <spotty/data/JsonTreeModel.h>

#include <gtest/gtest.h>

#include <QJsonDocument>

using namespace spotty;

namespace {

constexpr qint64 ms(qint64 value)
{
    return value * 1'000'000;
}

QJsonDocument json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text));
}

/// \brief Индекс узла по пути через точку; -1, если такого нет.
int findPath(const JsonTreeModel &model, const QString &path)
{
    for (int i = 1; i < model.arenaSize(); ++i) {
        if (model.isValidNode(i) && model.path(i) == path)
            return i;
    }
    return -1;
}

/// \brief Текст значения по пути; пусто, если узла нет.
QString valueAt(const JsonTreeModel &model, const QString &path)
{
    const int index = findPath(model, path);
    return index < 0 ? QString() : model.node(index).value;
}

} // namespace

TEST(JsonTreeModel, FlatObjectBecomesLeaves)
{
    JsonTreeModel model;
    ASSERT_TRUE(model.feed(json(R"({"temp":23.5,"ok":true})"), 0));

    EXPECT_EQ(model.nodeCount(), 2);
    EXPECT_EQ(valueAt(model, QStringLiteral("temp")), QStringLiteral("23.5"));
    EXPECT_EQ(valueAt(model, QStringLiteral("ok")), QStringLiteral("true"));
}

TEST(JsonTreeModel, NestedObjectBecomesBranches)
{
    JsonTreeModel model;
    ASSERT_TRUE(model.feed(json(R"({"status":{"ok":true,"code":0}})"), 0));

    const int branch = findPath(model, QStringLiteral("status"));
    ASSERT_GE(branch, 0);
    EXPECT_EQ(model.node(branch).kind, JsonNodeKind::Object);
    EXPECT_EQ(model.node(branch).children.size(), 2);
    EXPECT_EQ(valueAt(model, QStringLiteral("status.code")), QStringLiteral("0"));
}

TEST(JsonTreeModel, IntegersPrintWithoutDecimals)
{
    JsonTreeModel model;
    model.feed(json(R"({"n":12,"f":1.25})"), 0);

    EXPECT_EQ(valueAt(model, QStringLiteral("n")), QStringLiteral("12"));
    EXPECT_EQ(valueAt(model, QStringLiteral("f")), QStringLiteral("1.25"));
}

TEST(JsonTreeModel, AmbiguousStringsKeepTheirQuotes)
{
    // Строка «12» и число 12 обязаны различаться на экране, иначе по колонке значений
    // нельзя понять, что прислало устройство.
    JsonTreeModel model;
    model.feed(json(R"({"a":"12","b":12,"c":"true","d":"hello"})"), 0);

    EXPECT_EQ(valueAt(model, QStringLiteral("a")), QStringLiteral("\"12\""));
    EXPECT_EQ(valueAt(model, QStringLiteral("b")), QStringLiteral("12"));
    EXPECT_EQ(valueAt(model, QStringLiteral("c")), QStringLiteral("\"true\""));
    // Обычной строке кавычки не нужны — они были бы шумом на каждой строке.
    EXPECT_EQ(valueAt(model, QStringLiteral("d")), QStringLiteral("hello"));
}

TEST(JsonTreeModel, ScalarArrayStaysOnOneLine)
{
    JsonTreeModel model;
    model.feed(json(R"({"rgb":[1,2,3]})"), 0);

    const int node = findPath(model, QStringLiteral("rgb"));
    ASSERT_GE(node, 0);
    EXPECT_TRUE(model.node(node).children.isEmpty());
    EXPECT_EQ(model.node(node).value, QStringLiteral("[1,2,3]"));
}

TEST(JsonTreeModel, EmptyContainersAreShownNotSkipped)
{
    // Опустошение — событие. Без строки исчезновение содержимого выглядело бы как пропажа
    // поля, то есть как ошибка панели.
    JsonTreeModel model;
    model.feed(json(R"({"o":{},"a":[]})"), 0);

    EXPECT_EQ(valueAt(model, QStringLiteral("o")), QStringLiteral("{}"));
    EXPECT_EQ(valueAt(model, QStringLiteral("a")), QStringLiteral("[]"));
}

TEST(JsonTreeModel, ArrayOfObjectsCollapsesWithoutIdentityKey)
{
    JsonTreeModel model;
    ASSERT_TRUE(model.feed(json(R"({"s":[{"id":1,"v":10},{"id":2,"v":20}]})"), 0));

    // Одна ветка, значения — из последнего элемента.
    EXPECT_EQ(valueAt(model, QStringLiteral("s.id")), QStringLiteral("2"));
    EXPECT_EQ(valueAt(model, QStringLiteral("s.v")), QStringLiteral("20"));

    // И, главное, частота считается один раз на документ, а не по разу на элемент.
    const int node = findPath(model, QStringLiteral("s.v"));
    ASSERT_GE(node, 0);
    EXPECT_EQ(model.node(node).updates, 1u);
}

TEST(JsonTreeModel, IdentityKeySplitsArrayIntoBranches)
{
    JsonTreeModel model;
    model.setIdentityKey(QStringLiteral("id"));
    model.feed(json(R"({"s":[{"id":1,"v":10},{"id":2,"v":20}]})"), 0);

    EXPECT_EQ(valueAt(model, QStringLiteral("s.1.v")), QStringLiteral("10"));
    EXPECT_EQ(valueAt(model, QStringLiteral("s.2.v")), QStringLiteral("20"));
}

TEST(JsonTreeModel, ElementWithoutIdentityKeyGetsItsOwnBranch)
{
    JsonTreeModel model;
    model.setIdentityKey(QStringLiteral("id"));
    model.feed(json(R"({"s":[{"v":10}]})"), 0);

    const QString path = QStringLiteral("s.") + JsonTreeModel::noIdentityName()
        + QStringLiteral(".v");
    EXPECT_EQ(valueAt(model, path), QStringLiteral("10"));
}

TEST(JsonTreeModel, ChangingIdentityKeyRebuildsTheTree)
{
    JsonTreeModel model;
    model.feed(json(R"({"s":[{"id":1,"v":10}]})"), 0);
    ASSERT_GT(model.nodeCount(), 0);

    int resets = 0;
    QObject::connect(&model, &JsonTreeModel::modelReset, [&resets] { ++resets; });
    model.setIdentityKey(QStringLiteral("id"));

    EXPECT_EQ(resets, 1);
    EXPECT_EQ(model.nodeCount(), 0);
}

TEST(JsonTreeModel, RepeatedIdentityInOneDocumentCountsOnce)
{
    // Два элемента с одним id — это по-прежнему один документ, и частота обязана вырасти
    // на единицу. Иначе счётчик показывал бы темп, которого в потоке нет.
    JsonTreeModel model;
    model.setIdentityKey(QStringLiteral("id"));
    model.feed(json(R"({"s":[{"id":1,"v":10},{"id":1,"v":11}]})"), 0);

    const int node = findPath(model, QStringLiteral("s.1.v"));
    ASSERT_GE(node, 0);
    EXPECT_EQ(model.node(node).updates, 1u);
    EXPECT_EQ(model.node(node).value, QStringLiteral("11"));
}

TEST(JsonTreeModel, RateMatchesTheDocumentInterval)
{
    JsonTreeModel model;
    for (int i = 0; i < 10; ++i)
        model.feed(json(R"({"v":1})"), ms(100 * i));

    const int node = findPath(model, QStringLiteral("v"));
    ASSERT_GE(node, 0);
    EXPECT_NEAR(model.rate(node, ms(900)), 10.0, 0.5);
}

TEST(JsonTreeModel, FirstDocumentGivesNoRate)
{
    JsonTreeModel model;
    model.feed(json(R"({"v":1})"), 0);

    const int node = findPath(model, QStringLiteral("v"));
    ASSERT_GE(node, 0);
    // Интервала ещё не существует, и любое число было бы выдумкой.
    EXPECT_DOUBLE_EQ(model.rate(node, 0), 0.0);
}

TEST(JsonTreeModel, RateDecaysWhileTheFieldIsSilent)
{
    // Замершее поле обязано быть видно, а отдельной колонки «когда обновлялось» нет — эту
    // работу делает падающая частота.
    JsonTreeModel model;
    for (int i = 0; i < 10; ++i)
        model.feed(json(R"({"v":1})"), ms(100 * i));

    const int node = findPath(model, QStringLiteral("v"));
    ASSERT_GE(node, 0);

    const double live = model.rate(node, ms(900));
    const double after5s = model.rate(node, ms(900) + ms(5000));
    const double after100s = model.rate(node, ms(900) + ms(100000));

    EXPECT_NEAR(after5s, 0.2, 0.01);
    EXPECT_NEAR(after100s, 0.01, 0.001);
    // Строго убывает, без скачка в точке перехода к затуханию.
    EXPECT_GT(live, after5s);
    EXPECT_GT(after5s, after100s);
}

TEST(JsonTreeModel, SlowerFieldGetsLowerRate)
{
    JsonTreeModel model;
    for (int i = 0; i < 20; ++i) {
        // Медленное поле приходит через раз.
        if (i % 2 == 0)
            model.feed(json(R"({"fast":1,"slow":1})"), ms(100 * i));
        else
            model.feed(json(R"({"fast":1})"), ms(100 * i));
    }

    const int fast = findPath(model, QStringLiteral("fast"));
    const int slow = findPath(model, QStringLiteral("slow"));
    ASSERT_GE(fast, 0);
    ASSERT_GE(slow, 0);

    const qint64 now = ms(1900);
    EXPECT_NEAR(model.rate(fast, now), 10.0, 0.5);
    EXPECT_NEAR(model.rate(slow, now), 5.0, 0.5);
}

TEST(JsonTreeModel, ChangeStampMovesOnlyWhenTextDiffers)
{
    // Поле, честно рапортующее одно и то же сто раз в секунду, не должно мигать.
    JsonTreeModel model;
    model.feed(json(R"({"v":1})"), 0);

    const int node = findPath(model, QStringLiteral("v"));
    ASSERT_GE(node, 0);
    const qint64 firstChange = model.node(node).lastChangeNs;

    for (int i = 1; i < 10; ++i)
        model.feed(json(R"({"v":1})"), ms(100 * i));
    EXPECT_EQ(model.node(node).lastChangeNs, firstChange);

    model.feed(json(R"({"v":2})"), ms(2000));
    EXPECT_EQ(model.node(node).lastChangeNs, ms(2000));
}

TEST(JsonTreeModel, NodeLimitStopsNewPathsButKeepsUpdatingOldOnes)
{
    JsonTreeModel model;
    model.setMaxNodes(3);

    model.feed(json(R"({"a":1,"b":2,"c":3,"d":4,"e":5})"), 0);
    EXPECT_EQ(model.nodeCount(), 3);
    EXPECT_TRUE(model.truncated());
    EXPECT_GT(model.rejectedPaths(), 0u);

    // Попавшие в дерево продолжают жить полноценно.
    model.feed(json(R"({"a":9})"), ms(100));
    EXPECT_EQ(valueAt(model, QStringLiteral("a")), QStringLiteral("9"));
}

TEST(JsonTreeModel, DepthLimitCollapsesDeepSubtrees)
{
    JsonTreeModel model;
    model.setMaxDepth(2);
    model.feed(json(R"({"a":{"b":{"c":{"d":1}}}})"), 0);

    const int node = findPath(model, QStringLiteral("a.b"));
    ASSERT_GE(node, 0);
    EXPECT_TRUE(model.node(node).children.isEmpty());
    EXPECT_FALSE(model.node(node).value.isEmpty());
}

TEST(JsonTreeModel, ChildLimitIsPerBranch)
{
    JsonTreeModel model;
    model.setMaxChildren(2);
    model.feed(json(R"({"a":{"x":1,"y":2,"z":3},"b":{"x":1,"y":2,"z":3}})"), 0);

    const int a = findPath(model, QStringLiteral("a"));
    const int b = findPath(model, QStringLiteral("b"));
    ASSERT_GE(a, 0);
    ASSERT_GE(b, 0);
    // Взрывается обычно один массив, а не всё дерево, поэтому предел локальный.
    EXPECT_EQ(model.node(a).children.size(), 2);
    EXPECT_EQ(model.node(b).children.size(), 2);
}

TEST(JsonTreeModel, ClearDropsEverything)
{
    JsonTreeModel model;
    model.feed(json(R"({"a":1,"b":{"c":2}})"), 0);
    ASSERT_GT(model.nodeCount(), 0);

    model.clear();
    EXPECT_EQ(model.nodeCount(), 0);
    EXPECT_TRUE(model.rootChildren().isEmpty());
    EXPECT_FALSE(model.truncated());
}

TEST(JsonTreeModel, PruneStaleKeepsFreshBranchesAndSurvivorIndexes)
{
    JsonTreeModel model;
    for (int i = 0; i < 5; ++i)
        model.feed(json(R"({"old":1,"new":1})"), ms(100 * i));

    const int fresh = findPath(model, QStringLiteral("new"));
    ASSERT_GE(fresh, 0);

    // «new» продолжает приходить, «old» замолчал.
    for (int i = 5; i < 40; ++i)
        model.feed(json(R"({"new":1})"), ms(100 * i));

    const qint64 now = ms(4000);
    const QList<int> removed = model.pruneStale(now);

    EXPECT_EQ(removed.size(), 1);
    EXPECT_EQ(findPath(model, QStringLiteral("old")), -1);
    // Индексы выживших устойчивы — на этом держится всё, что смотрит на модель снаружи.
    EXPECT_EQ(findPath(model, QStringLiteral("new")), fresh);
}

TEST(JsonTreeModel, DirtyListHoldsOnlyChangedNodes)
{
    JsonTreeModel model;
    model.feed(json(R"({"a":1,"b":2})"), 0);
    model.takeDirty();

    model.feed(json(R"({"a":1,"b":3})"), ms(100));
    const QList<int> dirty = model.takeDirty();

    ASSERT_EQ(dirty.size(), 1);
    EXPECT_EQ(model.path(dirty.first()), QStringLiteral("b"));
    // Забрали — значит опустошили: второй вызов подряд ничего не отдаёт.
    EXPECT_TRUE(model.takeDirty().isEmpty());
}

TEST(JsonTreeModel, TopLevelArrayIsAccepted)
{
    JsonTreeModel model;
    model.setIdentityKey(QStringLiteral("id"));
    ASSERT_TRUE(model.feed(json(R"([{"id":1,"v":10},{"id":2,"v":20}])"), 0));

    EXPECT_EQ(valueAt(model, QStringLiteral("1.v")), QStringLiteral("10"));
    EXPECT_EQ(valueAt(model, QStringLiteral("2.v")), QStringLiteral("20"));
}

TEST(JsonTreeModel, MaxRateIsCachedBetweenCloseCalls)
{
    JsonTreeModel model;
    for (int i = 0; i < 10; ++i)
        model.feed(json(R"({"v":1})"), ms(100 * i));

    const qint64 now = ms(900);
    const double first = model.maxRate(now);
    // Второй вызов почти в тот же момент обязан вернуть то же число, не обходя арену заново.
    EXPECT_DOUBLE_EQ(model.maxRate(now + ms(1)), first);
    EXPECT_GT(first, 0.0);
}

TEST(JsonTreeModel, NonContainerDocumentIsRejected)
{
    JsonTreeModel model;
    EXPECT_FALSE(model.feed(QJsonDocument(), 0));
    EXPECT_EQ(model.nodeCount(), 0);
}
