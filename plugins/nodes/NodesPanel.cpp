/**
 * \file NodesPanel.cpp
 * \brief Список не сортируется повторно: возвращение узла сохраняет его место.
 */
#include "NodesPanel.h"
#include <spotty/ui/IPanelHost.h>
#include <QHeaderView>
#include <QDateTime>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QCheckBox>
#include <QMenu>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace spotty {
namespace {
/// \brief Фиксированные цвета позволяют различать присутствие независимо от темы.
QIcon statusIcon(bool online, bool rebooting = false)
{
    QPixmap image(14, 14);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(rebooting
        ? ((QDateTime::currentMSecsSinceEpoch() / 250) % 2
            ? QColor(56, 153, 248) : QColor(25, 75, 125))
        : (online ? QColor(109, 183, 116) : QColor(170, 112, 217)));
    painter.drawEllipse(QRectF(2, 2, 10, 10));
    return QIcon(image);
}
} // namespace

NodesPanel::NodesPanel(IPanelHost *panelHost, QWidget *parent)
    : PanelWidget(panelHost, parent)
    , m_known(panelHost->value(QStringLiteral("known")).toMap())
    , m_names(panelHost->value(QStringLiteral("names")).toMap())
{
    setPanelTitle(tr("Nodes"));
    m_common = new QCheckBox(tr("Common history"), this);
    m_common->setObjectName(QStringLiteral("commonNodeHistory"));
    m_common->setChecked(host()->commonNodeHistory());
    m_common->setToolTip(tr("Show all output instead of the selected node's history"));
    connect(m_common, &QCheckBox::toggled, this,
            [this](bool common) { host()->setCommonNodeHistory(common); });
    auto *historyRow = new QHBoxLayout;
    historyRow->addWidget(m_common, 1);
    auto *clearNames = new QPushButton(tr("Clear names"), this);
    clearNames->setObjectName(QStringLiteral("clearNodeNames"));
    historyRow->addWidget(clearNames);
    connect(clearNames, &QPushButton::clicked, this, [this] {
        m_names.clear();
        host()->setValue(QStringLiteral("names"), m_names);
        for (const auto &view : m_adapters) {
            const QSignalBlocker blocker(view.table);
            for (int row = 0; row < view.table->rowCount(); ++row)
                view.table->item(row, 2)->setText(QString());
        }
    });
    content()->addLayout(historyRow);
    m_empty = new QLabel(tr("No network adapters available"), this);
    m_empty->setObjectName(QStringLiteral("hintLabel"));
    m_empty->setWordWrap(true);
    content()->addWidget(m_empty);
    m_adaptersLayout = new QVBoxLayout;
    content()->addLayout(m_adaptersLayout, 1);
    auto *clear = new QPushButton(tr("Clear list"), this);
    clear->setObjectName(QStringLiteral("clearNodeList"));
    connect(clear, &QPushButton::clicked, this, &NodesPanel::clearLists);
    content()->addWidget(clear);
    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &NodesPanel::refresh);
    connect(host(), &IPanelHost::aboutToClose, this, &NodesPanel::stopDiscovery);
    connect(host(), &IPanelHost::settingsReset, this, [this] {
        m_names.clear();
        m_common->setChecked(host()->commonNodeHistory());
        clearLists();
    });
}

NodesPanel::~NodesPanel()
{
    stopDiscovery();
}

NodesPanel::AdapterView NodesPanel::addAdapter(const QString &id)
{
    AdapterView view;
    view.widget = new QWidget(this);
    view.widget->setMinimumHeight(0);
    view.widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    auto *layout = new QVBoxLayout(view.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    view.title = new QLabel(view.widget);
    view.title->setWordWrap(true);
    layout->addWidget(view.title);
    view.table = new QTableWidget(0, 3, view.widget);
    view.table->setObjectName(QStringLiteral("nodesTable"));
    view.table->setProperty("adapterId", id);
    view.table->setHorizontalHeaderLabels({tr("№"), tr("Node"), tr("Name")});
    view.table->verticalHeader()->hide();
    view.table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    view.table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    view.table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    view.table->setSelectionBehavior(QAbstractItemView::SelectRows);
    view.table->setSelectionMode(QAbstractItemView::SingleSelection);
    view.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view.table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view.table, &QWidget::customContextMenuRequested, this,
            [this, table = view.table](const QPoint &pos) {
        auto *item = table->itemAt(pos);
        if (!item || item->column() != 2)
            return;
        QMenu menu(table);
        menu.addAction(tr("Rename"), table, [table, item] { table->editItem(item); });
        menu.exec(table->viewport()->mapToGlobal(pos));
    });
    view.table->setMinimumHeight(0);
    view.table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    layout->addWidget(view.table, 1);
    m_adaptersLayout->addWidget(view.widget, 1);
    connect(view.table, &QTableWidget::cellClicked, this, [this, id, table = view.table](int row) {
        host()->selectNetworkNode(id, table->item(row, 1)->data(Qt::UserRole).toInt());
    });
    connect(view.table, &QTableWidget::itemChanged, this, [this, id](QTableWidgetItem *item) {
        if (item->column() != 2)
            return;
        auto names = m_names.value(id).toMap();
        names.insert(QString::number(item->data(Qt::UserRole).toInt()), item->text());
        m_names.insert(id, names);
        host()->setValue(QStringLiteral("names"), m_names);
    });
    return view;
}

void NodesPanel::refresh()
{
    const auto adapters = host()->networkAdapters();
    m_empty->setVisible(adapters.isEmpty());
    QSet<QString> present;
    bool changed = false;
    for (const auto &adapter : adapters) {
        present.insert(adapter.id);
        if (!m_adapters.contains(adapter.id))
            m_adapters.insert(adapter.id, addAdapter(adapter.id));
        auto &view = m_adapters[adapter.id];
        view.title->setText(adapter.name);
        view.title->setToolTip(adapter.id);
        const auto nodes = host()->networkNodes(adapter.id, true);
        QVariantList known = m_known.value(adapter.id).toList();
        QSet<int> online;
        for (const auto &node : nodes) {
            if (!known.contains(node.number)) {
                known.append(node.number);
                changed = true;
            }
            if (node.online)
                online.insert(node.number);
        }
        m_known.insert(adapter.id, known);
        const auto names = m_names.value(adapter.id).toMap();
        const QSignalBlocker blocker(view.table);
        view.table->setRowCount(int(known.size()));
        for (int row = 0; row < known.size(); ++row) {
            const int node = known.at(row).toInt();
            if (!view.table->item(row, 0)) {
                auto *ordinal = new QTableWidgetItem(QString::number(row + 1));
                ordinal->setFlags(ordinal->flags() & ~Qt::ItemIsEditable);
                view.table->setItem(row, 0, ordinal);
                auto *number = new QTableWidgetItem(QString::number(node));
                number->setData(Qt::UserRole, node);
                number->setFlags(number->flags() & ~Qt::ItemIsEditable);
                view.table->setItem(row, 1, number);
                auto *name = new QTableWidgetItem(names.value(QString::number(node)).toString());
                name->setData(Qt::UserRole, node);
                name->setToolTip(tr("Right-click to rename"));
                view.table->setItem(row, 2, name);
            }
            auto *ordinal = view.table->item(row, 0);
            ordinal->setIcon(statusIcon(online.contains(node), host()->nodeRebooting(adapter.id, node)));
            ordinal->setToolTip(host()->nodeRebooting(adapter.id, node) ? tr("Rebooting")
                : (online.contains(node) ? tr("Online") : tr("No response")));
            QFont font = ordinal->font();
            font.setBold(host()->interfaceId() == adapter.id && node == adapter.selectedNode);
            ordinal->setFont(font);
        }
    }
    for (auto it = m_adapters.begin(); it != m_adapters.end();) {
        if (present.contains(it.key())) {
            ++it;
        } else {
            host()->networkNodes(it.key(), false);
            delete it->widget;
            it = m_adapters.erase(it);
        }
    }
    if (changed)
        host()->setValue(QStringLiteral("known"), m_known);
}

void NodesPanel::stopDiscovery()
{
    m_timer->stop();
    for (auto it = m_adapters.cbegin(); it != m_adapters.cend(); ++it)
        host()->networkNodes(it.key(), false);
}

void NodesPanel::clearLists()
{
    m_known.clear();
    host()->setValue(QStringLiteral("known"), m_known);
    for (auto it = m_adapters.begin(); it != m_adapters.end(); ++it) {
        host()->clearNetworkNodes(it.key());
        const QSignalBlocker blocker(it->table);
        it->table->setRowCount(0);
    }
}

void NodesPanel::showEvent(QShowEvent *event)
{
    PanelWidget::showEvent(event);
    m_common->setChecked(host()->commonNodeHistory());
    refresh();
    m_timer->start();
}

void NodesPanel::hideEvent(QHideEvent *event)
{
    stopDiscovery();
    PanelWidget::hideEvent(event);
}
} // namespace spotty
