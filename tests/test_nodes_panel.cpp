#include <QMouseEvent>
#include <QKeyEvent>
#include <spotty/ui/MdiCodepoints.h>
/**
 * \file test_nodes_panel.cpp
 * \brief Проводка списка узлов проверяется без CAN и рабочего стола.
 */
#include "../src/ui/RebootIndicator.h"
#include "../plugins/nodes/NodesPanel.h"
#include "support/FakePanelHost.h"
#include "support/TestSupport.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QPushButton>
#include <QCheckBox>
#include <QToolButton>
#include <QMenu>
#include <QTimer>
#include <QLineEdit>
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
    QHash<QString, bool> adapterEnabled;
    bool discoveryEnabled = true;
    QStringList scanRequests;
    void scanNetworkOnce(const QString &id) override { scanRequests.append(id); }
    bool networkAdapterEnabled(const QString &id) const override
    { return adapterEnabled.value(id, true); }
    void setNetworkAdapterEnabled(const QString &id, bool enabled) override
    { adapterEnabled[id] = enabled; }
    void setNodeDiscoveryEnabled(bool enabled) override { discoveryEnabled = enabled; }
    bool common = false;
    bool rebooting = false;
    bool nodeRebooting(const QString &adapter, int node) const override
    { return rebooting && adapter == QLatin1String("a") && node == 12; }
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
        auto *common = panel.findChild<QCheckBox *>(QStringLiteral("commonNodeHistory"));
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

TEST(NodesPanel, ContextRenameDoesNotConnectToTheEditedNode)
{
    TempDir dir;
    NodesHost host(dir.path());
    NodesPanel panel(&host);
    panel.resize(310, 700);
    panel.show();
    auto *table = tableFor(panel, QStringLiteral("a"));
    ASSERT_NE(table, nullptr);
    auto *item = table->item(0, 2);
    QTimer::singleShot(0, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        ASSERT_NE(menu, nullptr);
        menu->actions().first()->trigger();
        menu->close();
    });
    QMetaObject::invokeMethod(table, "customContextMenuRequested",
                             Q_ARG(QPoint, table->visualItemRect(item).center()));
    EXPECT_EQ(host.selectedNode, 0);
    auto *editor = table->findChild<QLineEdit *>();
    ASSERT_NE(editor, nullptr);
    editor->setText(QStringLiteral("Renamed"));
    table->setFocus();
    QApplication::processEvents();
    EXPECT_EQ(host.selectedNode, 0);
}

TEST(NodesPanel, ClearsNamesWithoutClearingNodesOrConnecting)
{
    TempDir dir;
    NodesHost host(dir.path());
    NodesPanel panel(&host);
    panel.show();
    auto *table = tableFor(panel, QStringLiteral("a"));
    ASSERT_NE(table, nullptr);
    table->item(0, 2)->setText(QStringLiteral("Battery"));
    auto *button = panel.findChild<QToolButton *>(QStringLiteral("clearNodeNames"));
    ASSERT_NE(button, nullptr);
    button->click();
    EXPECT_EQ(table->rowCount(), 2);
    EXPECT_TRUE(table->item(0, 2)->text().isEmpty());
    EXPECT_TRUE(host.settings.value(QStringLiteral("names")).toMap().isEmpty());
    EXPECT_EQ(host.selectedNode, 0);
}

TEST(NodesPanel, ShowsRebootStatusOnlyForTheAffectedNode)
{
    TempDir dir;
    NodesHost host(dir.path());
    host.rebooting = true;
    NodesPanel panel(&host);
    panel.show();
    auto *table = tableFor(panel, QStringLiteral("a"));
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->item(0, 0)->toolTip(), QStringLiteral("Rebooting"));
    EXPECT_EQ(table->item(1, 0)->toolTip(), QStringLiteral("Online"));
    host.rebooting = false;
    refresh(panel);
    EXPECT_EQ(table->item(0, 0)->toolTip(), QStringLiteral("Online"));
}

TEST(RebootIndicator, ImmediateAcknowledgementRemainsVisibleForOneCycle)
{
    RebootIndicator indicator(1000, 3000);
    indicator.answered(1001);
    EXPECT_TRUE(indicator.active(1250));
    EXPECT_TRUE(indicator.active(1499));
    EXPECT_FALSE(indicator.active(1500));
}
TEST(RebootIndicator, ResponseAndTimeoutStopIndicationWithoutExtendingTimeout)
{
    RebootIndicator indicator(1000, 3000);
    EXPECT_TRUE(indicator.active(3999));
    EXPECT_FALSE(indicator.active(4000));
    indicator.answered(2000);
    EXPECT_FALSE(indicator.active(2000));
    RebootIndicator shortTimeout(1000, 100);
    shortTimeout.answered(1001);
    EXPECT_FALSE(shortTimeout.active(1100));
    RebootIndicator disabledTimeout(1000, 0);
    EXPECT_TRUE(disabledTimeout.active(100000));
    disabledTimeout.answered(100000);
    EXPECT_FALSE(disabledTimeout.active(100000));
}

TEST(NodesPanel, DisconnectRemainsEffectiveWhilePanelIsVisible)
{
    TempDir dir;
    NodesHost host(dir.path());
    NodesPanel panel(&host);
    panel.show();
    auto *table = tableFor(panel, QStringLiteral("a"));
    ASSERT_NE(table, nullptr);
    auto *button = table->parentWidget()->findChild<QToolButton *>(
        QStringLiteral("nodeAdapterConnection"));
    ASSERT_NE(button, nullptr);
    button->click();
    EXPECT_FALSE(host.networkAdapterEnabled(QStringLiteral("a")));
    EXPECT_FALSE(host.discovering.value(QStringLiteral("a")));
    refresh(panel);
    EXPECT_FALSE(host.discovering.value(QStringLiteral("a")));
    button->click();
    EXPECT_TRUE(host.networkAdapterEnabled(QStringLiteral("a")));
    EXPECT_TRUE(host.discovering.value(QStringLiteral("a")));
}
TEST(NodesPanel, SingleNodeModeDisablesBroadcastAndPersistsSelection)
{
    TempDir dir;
    NodesHost host(dir.path());
    {
        NodesPanel panel(&host);
        panel.show();
        auto *button = panel.findChild<QToolButton *>(QStringLiteral("singleNodeOnly"));
        ASSERT_NE(button, nullptr);
        button->click();
        EXPECT_FALSE(host.discoveryEnabled);
        EXPECT_TRUE(host.settings.value(QStringLiteral("singleNodeOnly")).toBool());
        EXPECT_EQ(host.selectedNode, 0);
    }
    NodesPanel panel(&host);
    EXPECT_FALSE(host.discoveryEnabled);
}

TEST(NodesPanel, HexadecimalFormatChangesOnlyDisplayAndPersists)
{
    TempDir dir;
    NodesHost host(dir.path());
    {
        NodesPanel panel(&host);
        panel.show();
        auto *table = tableFor(panel, QStringLiteral("a"));
        ASSERT_NE(table, nullptr);
        auto *hex = panel.findChild<QToolButton *>(QStringLiteral("hexNodeNumbers"));
        ASSERT_NE(hex, nullptr);
        EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("12"));
        hex->click();
        EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("0xC"));
        EXPECT_EQ(table->item(0, 1)->data(Qt::UserRole).toInt(), 12);
        EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("1"));
        QMetaObject::invokeMethod(table, "cellClicked", Q_ARG(int, 0), Q_ARG(int, 1));
        EXPECT_EQ(host.selectedNode, 12);
        EXPECT_TRUE(host.settings.value(QStringLiteral("hexNodeNumbers")).toBool());
    }
    NodesPanel restored(&host);
    restored.show();
    auto *table = tableFor(restored, QStringLiteral("a"));
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("0xC"));
    auto *hex = restored.findChild<QToolButton *>(QStringLiteral("hexNodeNumbers"));
    ASSERT_NE(hex, nullptr);
    hex->click();
    EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("12"));
    EXPECT_TRUE(host.iconGlyphs.contains(mdi::TagRemoveOutline));
    EXPECT_TRUE(host.iconGlyphs.contains(mdi::Hexadecimal));
}

TEST(NodesPanel, DoubleClickRenamesWithoutConnectingAndSavesName)
{
    TempDir dir;
    NodesHost host(dir.path());
    NodesPanel panel(&host);
    panel.resize(310, 700);
    panel.show();
    QApplication::processEvents();
    auto *table = tableFor(panel, QStringLiteral("a"));
    ASSERT_NE(table, nullptr);
    const QPointF position(table->visualItemRect(table->item(0, 2)).center());
    for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease,
                      QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, position, table->viewport()->mapToGlobal(position.toPoint()),
                          Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(table->viewport(), &event);
    }
    EXPECT_EQ(host.selectedNode, 0);
    auto *editor = table->findChild<QLineEdit *>();
    ASSERT_NE(editor, nullptr);
    editor->setText(QStringLiteral("Renamed by double-click"));
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(editor, &enter);
    QApplication::processEvents();
    EXPECT_EQ(host.selectedNode, 0);
    EXPECT_EQ(host.settings.value(QStringLiteral("names")).toMap()
                  .value(QStringLiteral("a")).toMap().value(QStringLiteral("12")).toString(),
              QStringLiteral("Renamed by double-click"));
}

TEST(NodesPanel, RadarScansOnlyItsAdapterWithoutConnectingOrChangingSingleNodeFlag)
{
    TempDir dir;
    NodesHost host(dir.path());
    host.adapterEnabled[QStringLiteral("a")] = false;
    host.settings[QStringLiteral("singleNodeOnly")] = true;
    NodesPanel panel(&host);
    panel.show();
    auto *table = tableFor(panel, QStringLiteral("a"));
    ASSERT_NE(table, nullptr);
    auto *scan = table->parentWidget()->findChild<QToolButton *>(QStringLiteral("scanNetworkOnce"));
    ASSERT_NE(scan, nullptr);
    scan->click();
    EXPECT_EQ(host.scanRequests, QStringList{QStringLiteral("a")});
    EXPECT_FALSE(host.networkAdapterEnabled(QStringLiteral("a")));
    EXPECT_FALSE(host.discoveryEnabled);
    EXPECT_EQ(host.selectedNode, 0);
    EXPECT_TRUE(host.iconGlyphs.contains(mdi::Radar));
}
