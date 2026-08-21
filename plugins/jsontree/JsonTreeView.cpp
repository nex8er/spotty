/**
 * \file JsonTreeView.cpp
 * \brief Реализация spotty::JsonTreeView.
 */
#include "JsonTreeView.h"

#include "JsonRateDelegate.h"

#include <spotty/data/JsonTreeModel.h>
#include <spotty/ui/IPanelHost.h>

#include <QElapsedTimer>
#include <QHeaderView>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace spotty {

namespace {

enum Column { ColumnField = 0, ColumnValue, ColumnRate, ColumnCount };

/// \brief Ширина колонки частоты: число плюс полоска рядом с ним.
constexpr int kRateColumnWidth = 78;

/**
 * \brief Текущее значение монотонных часов в наносекундах.
 *
 * Те же часы, по которым модель получает отметки строк: сравнивать отметку прихода с
 * системным временем нельзя — они из разных систем отсчёта.
 */
qint64 monotonicNow()
{
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.nsecsElapsed();
}

} // namespace

JsonTreeView::JsonTreeView(IPanelHost *panelHost, JsonTreeModel *model, QWidget *parent)
    : QWidget(parent)
    , m_host(panelHost)
    , m_model(model)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(ColumnCount);
    m_tree->setHeaderLabels({tr("Field"), tr("Value"), tr("Rate")});
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    // Горизонтальная прокрутка запрещена: в узкой панели она появлялась бы всегда и прятала
    // колонку частоты. Длинные значения сокращаются многоточием — увидеть, что значение не
    // поместилось, лучше, чем не увидеть колонку вовсе.
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_tree->header()->setSectionResizeMode(ColumnField, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(ColumnValue, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(ColumnRate, QHeaderView::Fixed);
    m_tree->header()->resizeSection(ColumnRate, kRateColumnWidth);
    m_tree->header()->setStretchLastSection(false);
    // Подписки на sectionResized здесь намеренно нет: ширины задаём мы сами, и этот сигнал
    // приходил бы в ответ на собственную правку — записанная ловушка, петля не сходится.

    m_delegate = new JsonRateDelegate(m_model, this);
    m_tree->setItemDelegate(m_delegate);
    // Начальная отметка: до первого тика таймера дерево уже может быть отрисовано, и с
    // нулём вместо времени пришлось бы считать простой от начала времён.
    m_delegate->setFrameTime(monotonicNow());
    applyTheme();

    layout->addWidget(m_tree);

    m_structureTimer = new QTimer(this);
    m_structureTimer->setSingleShot(true);
    m_structureTimer->setInterval(kStructureIntervalMs);
    connect(m_structureTimer, &QTimer::timeout, this, &JsonTreeView::syncStructure);

    m_paintTimer = new QTimer(this);
    m_paintTimer->setSingleShot(true);
    m_paintTimer->setInterval(kPaintIntervalMs);
    connect(m_paintTimer, &QTimer::timeout, this, &JsonTreeView::paintTick);

    // Оба обработчика только взводят таймер: путь данных не касается виджетов.
    connect(m_model, &JsonTreeModel::changed, this, [this] {
        m_lastDocumentNs = monotonicNow();
        scheduleStructure();
        schedulePaint();
    });
    connect(m_model, &JsonTreeModel::nodesAdded, this, &JsonTreeView::scheduleStructure);
    connect(m_model, &JsonTreeModel::nodesRemoved, this, &JsonTreeView::removeNodes);
    connect(m_model, &JsonTreeModel::modelReset, this, &JsonTreeView::rebuild);

    // Раскрытие ветки — прямое действие пользователя, и ждать общего таймера здесь нельзя:
    // задержка в 120 мс под рукой читается как рывок.
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this] { m_tree->viewport()->update(); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this] { m_tree->viewport()->update(); });
}

void JsonTreeView::applyTheme()
{
    if (!m_host)
        return;
    m_delegate->setColors(m_host->color(IPanelHost::ColorRole::Text),
                          m_host->color(IPanelHost::ColorRole::TextMuted),
                          m_host->color(IPanelHost::ColorRole::Accent),
                          m_host->color(IPanelHost::ColorRole::Rx),
                          m_host->color(IPanelHost::ColorRole::Base));
    m_tree->viewport()->update();
}

void JsonTreeView::setFlashEnabled(bool enabled)
{
    m_delegate->setFlashEnabled(enabled);
    m_tree->viewport()->update();
}

void JsonTreeView::setFlashDurationMs(int milliseconds)
{
    m_delegate->setFlashDurationMs(milliseconds);
    m_tree->viewport()->update();
}

int JsonTreeView::flashDurationMs() const
{
    return m_delegate->flashDurationMs();
}

void JsonTreeView::setHideStale(bool hide)
{
    if (m_hideStale == hide)
        return;
    m_hideStale = hide;
    syncStructure();
}

void JsonTreeView::expandTree()
{
    m_tree->expandAll();
}

void JsonTreeView::collapseTree()
{
    m_tree->collapseAll();
}

int JsonTreeView::nodeAt(const QPoint &viewportPosition) const
{
    const QTreeWidgetItem *item = m_tree->itemAt(viewportPosition);
    return item ? item->data(ColumnField, JsonRateDelegate::kNodeRole).toInt() : -1;
}

void JsonTreeView::scheduleStructure()
{
    if (!m_structureTimer->isActive())
        m_structureTimer->start(kStructureIntervalMs);
}

void JsonTreeView::schedulePaint()
{
    if (!m_paintTimer->isActive())
        m_paintTimer->start(kPaintIntervalMs);
}

void JsonTreeView::rebuild()
{
    m_tree->clear();
    m_items.clear();
    syncStructure();
}

QTreeWidgetItem *JsonTreeView::ensureItem(int node)
{
    const auto it = m_items.constFind(node);
    if (it != m_items.constEnd())
        return *it;

    if (!m_model->isValidNode(node))
        return nullptr;

    const int parent = m_model->node(node).parent;
    QTreeWidgetItem *item = nullptr;
    if (parent <= 0) {
        item = new QTreeWidgetItem(m_tree);
    } else {
        QTreeWidgetItem *parentItem = ensureItem(parent);
        if (!parentItem)
            return nullptr;
        item = new QTreeWidgetItem(parentItem);
        // Ветки раскрыты сразу: свёрнутое дерево прятало бы ровно то, ради чего панель и
        // открывают, и заставляло бы раскрывать каждую новую ветку руками.
        parentItem->setExpanded(true);
    }

    // Роль пишется во все колонки, а не только в первую: делегат получает индекс той
    // ячейки, которую рисует, и в колонке частоты роль из соседней ему недоступна. Без
    // этого колонка частоты оставалась пустой — узел казался невалидным.
    for (int column = 0; column < ColumnCount; ++column)
        item->setData(column, JsonRateDelegate::kNodeRole, node);

    m_items.insert(node, item);
    return item;
}

void JsonTreeView::fillItem(QTreeWidgetItem *item, int node)
{
    const JsonNode &n = m_model->node(node);
    item->setText(ColumnField, n.name);
    item->setText(ColumnValue, n.value);
    item->setToolTip(ColumnField, m_model->path(node));
    item->setToolTip(ColumnValue, n.value);
}

void JsonTreeView::syncStructure()
{
    const qint64 now = monotonicNow();

    // Сначала новые узлы: обход арены дешевле, чем кажется, и делается он восемь раз в
    // секунду, а не на каждый документ.
    for (int i = 1; i < m_model->arenaSize(); ++i) {
        if (!m_model->isValidNode(i) || m_items.contains(i))
            continue;
        if (QTreeWidgetItem *item = ensureItem(i))
            fillItem(item, i);
    }

    // Затем изменившиеся значения — только они, а не всё дерево.
    const QList<int> dirty = m_model->takeDirty();
    for (const int node : dirty) {
        const auto it = m_items.constFind(node);
        if (it != m_items.constEnd() && m_model->isValidNode(node))
            fillItem(*it, node);
    }

    if (m_hideStale) {
        for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
            if (m_model->isValidNode(it.key()))
                it.value()->setHidden(m_model->isStale(it.key(), now));
        }
    } else {
        for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
            it.value()->setHidden(false);
    }

    schedulePaint();
}

void JsonTreeView::removeNodes(const QList<int> &nodes)
{
    // Элементы удаляются до того, как модель переиспользует освободившийся слот: оба
    // действия идут в потоке интерфейса, поэтому промежуточного состояния не возникает.
    for (const int node : nodes) {
        const auto it = m_items.constFind(node);
        if (it == m_items.constEnd())
            continue;
        delete *it;
        m_items.erase(it);
    }
    m_tree->viewport()->update();
}

bool JsonTreeView::animationAlive(qint64 nowNs) const
{
    const qint64 flashNs = qint64(m_delegate->flashDurationMs()) * 1'000'000;
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        if (it.value()->isHidden() || !m_model->isValidNode(it.key()))
            continue;
        // Достаточно одной живой строки: остальные всё равно перерисуются тем же кадром.
        if (m_model->nsSinceChange(it.key(), nowNs) < flashNs)
            return true;
        if (m_model->rate(it.key(), nowNs) > 0.05)
            return true;
    }
    return false;
}

void JsonTreeView::paintTick()
{
    const qint64 now = monotonicNow();
    m_delegate->setFrameTime(now);
    m_tree->viewport()->update();

    // Затухание обязано догореть даже после того, как поток встал: частота падает сама, и
    // без следующего кадра число замерло бы на экране, не соответствуя действительности.
    if (!animationAlive(now))
        return;

    const bool idle = (now - m_lastDocumentNs) > qint64(kIdleAfterMs) * 1'000'000;
    m_paintTimer->start(idle ? kIdlePaintIntervalMs : kPaintIntervalMs);
}

} // namespace spotty
