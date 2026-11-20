/**
 * \file test_nodes_panel.cpp
 * \brief Проводка списка узлов проверяется без CAN и рабочего стола.
 */
#include "../plugins/nodes/NodesPanel.h"
#include "support/FakePanelHost.h"
#include "support/TestSupport.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QPushButton>
#include <QTableWidget>
#include <gtest/gtest.h>

using namespace spotty;
using namespace spotty::test;

namespace {
class NodesHost : public FakePanelHost
{
public:
    using FakePanelHost::FakePanelHost;
    QList<NetworkAdapter> adapters = {{QStringLiteral("a"), QStringLiteral("PCAN A"), 3},
                                     {QStringLiteral("b"), QStringLiteral("PCAN B"), 3}};
    QHash<QString, QList<NetworkNode>> nodes = {
        {QStringLiteral("a"), {{12, true}, {3, true}}},
        {QStringLiteral("b"), {{3, true}}}};
    QHash<QString, bool> discovering;
    QString selectedAdapter;
    int selectedNode = 0;
    bool common = false;
    QList<NetworkAdapter> networkAdapters() const override { return adapters; }
    QList<NetworkNode> networkNodes(const QString &id, bool scan) override
    {
        discovering[id] = scan;
        return nodes.value(id);
    }
    void clearNetworkNodes(const QString &id) override { nodes[id].clear(); }
    void selectNetworkNode(const QString &id, int number) override
    {
        selectedAdapter = id;
        selectedNode = number;
    }
    bool commonNodeHistory() const override { return common; }
    void setCommonNodeHistory(bool on) override { common = on; }
};

QTableWidget *tableFor(NodesPanel &panel, const QString &id)
{
    for (auto *table : panel.findChildren<QTableWidget *>()) {
        if (table->property("adapterId").toString() == id)
            return table;
    }
    return nullptr;
}

void refresh(NodesPanel &panel)
{
    panel.hide();
    panel.show();
    QApplication::processEvents();
}
} // namespace

TEST(NodesPanel, ShowsAdaptersWithEqualSpaceAndStopsDiscoveryWhenHidden)
{
    TempDir dir;
    NodesHost host(dir.path());
    NodesPanel panel(&host);
    panel.resize(310, 700);
    panel.show();
    QApplication::processEvents();
    auto *a = tableFor(panel, QStringLiteral("a"));
    auto *b = tableFor(panel, QStringLiteral("b"));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_LE(qAbs(a->parentWidget()->height() - b->parentWidget()->height()), 1);
    EXPECT_EQ(a->columnCount(), 3);
    EXPECT_EQ(a->rowCount(), 2);
    EXPECT_EQ(a->item(0, 1)->text(), QStringLiteral("12"));
    EXPECT_TRUE(host.discovering.value(QStringLiteral("a")));
    EXPECT_TRUE(host.discovering.value(QStringLiteral("b")));
    panel.hide();
    EXPECT_FALSE(host.discovering.value(QStringLiteral("a")));
    EXPECT_FALSE(host.discovering.value(QStringLiteral("b")));
}

TEST(NodesPanel, RetainsOfflineOrderSelectsOfflineNodesAndSavesNamesPerAdapter)
{
    TempDir dir;
    NodesHost host(dir.path());
    {
        NodesPanel panel(&host);
        panel.show();
        auto *a = tableFor(panel, QStringLiteral("a"));
        ASSERT_NE(a, nullptr);
        a->item(1, 2)->setText(QStringLiteral("Battery"));
        auto *b = tableFor(panel, QStringLiteral("b"));
        ASSERT_NE(b, nullptr);
        EXPECT_TRUE(b->item(0, 2)->text().isEmpty());
        host.nodes[QStringLiteral("a")] = {{12, false}, {3, true}, {1, true}};
        refresh(panel);
        EXPECT_EQ(a->item(0, 1)->text(), QStringLiteral("12"));
        EXPECT_EQ(a->item(0, 0)->toolTip(), QStringLiteral("No response"));
        EXPECT_EQ(a->item(2, 1)->text(), QStringLiteral("1"));
        QMetaObject::invokeMethod(a, "cellClicked", Q_ARG(int, 0), Q_ARG(int, 1));
        EXPECT_EQ(host.selectedAdapter, QStringLiteral("a"));
        EXPECT_EQ(host.selectedNode, 12);
        auto *common = panel.findChild<QPushButton *>(QStringLiteral("commonNodeHistory"));
        ASSERT_NE(common, nullptr);
        common->click();
        EXPECT_TRUE(host.common);
    }
    NodesPanel restored(&host);
    restored.show();
    auto *a = tableFor(restored, QStringLiteral("a"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->item(1, 2)->text(), QStringLiteral("Battery"));
    auto *clear = restored.findChild<QPushButton *>(QStringLiteral("clearNodeList"));
    ASSERT_NE(clear, nullptr);
    clear->click();
    EXPECT_EQ(a->rowCount(), 0);
    host.nodes[QStringLiteral("a")] = {{3, true}, {12, true}};
    refresh(restored);
    EXPECT_EQ(a->item(0, 1)->text(), QStringLiteral("3"));
    EXPECT_EQ(a->item(0, 2)->text(), QStringLiteral("Battery"));
}

TEST(NodesPanel, Snapshot)
{
    const QString path = qEnvironmentVariable("SPOTTY_TEST_SNAPSHOT_DIR");
    if (path.isEmpty())
        GTEST_SKIP();
    TempDir dir;
    NodesHost host(dir.path());
    host.nodes[QStringLiteral("a")] = {{12, true}, {3, false}, {8, true}};
    host.settings.insert(QStringLiteral("names"), QVariantMap{
        {QStringLiteral("a"), QVariantMap{{QStringLiteral("12"), QStringLiteral("Battery")},
                                         {QStringLiteral("3"), QStringLiteral("Charger")}}}});
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(SPOTTY_NODE_TEST_FONT));
    ASSERT_GE(fontId, 0);
    const auto families = QFontDatabase::applicationFontFamilies(fontId);
    ASSERT_FALSE(families.isEmpty());
    QApplication::setFont(QFont(families.first(), 9));
    QApplication::setStyle(QStringLiteral("Fusion"));
    const QString qssPath = qEnvironmentVariable("SPOTTY_TEST_QSS");
    QFile paletteFile(qssPath + QStringLiteral(".pal"));
    if (paletteFile.open(QIODevice::ReadOnly)) {
        const auto colors = QString::fromUtf8(paletteFile.readAll()).split(u' ', Qt::SkipEmptyParts);
        if (colors.size() >= 6) {
            QPalette palette;
            palette.setColor(QPalette::Window, QColor(colors[0]));
            palette.setColor(QPalette::WindowText, QColor(colors[1]));
            palette.setColor(QPalette::Base, QColor(colors[2]));
            palette.setColor(QPalette::Text, QColor(colors[1]));
            palette.setColor(QPalette::Button, QColor(colors[3]));
            palette.setColor(QPalette::ButtonText, QColor(colors[1]));
            palette.setColor(QPalette::Highlight, QColor(colors[4]));
            palette.setColor(QPalette::PlaceholderText, QColor(colors[5]));
            QApplication::setPalette(palette);
        }
    }
    QFile qss(qssPath);
    if (qss.open(QIODevice::ReadOnly))
        qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
    NodesPanel panel(&host);
    panel.resize(310, 700);
    panel.show();
    QApplication::processEvents();
    QDir().mkpath(path);
    EXPECT_TRUE(panel.grab().save(QDir(path).filePath(QStringLiteral("nodes.png"))));
}
