/**
 * \file JsonTreeView.h
 * \brief Дерево путей JSON: элементы, троттлинг перерисовки, затухание.
 */
#pragma once

#include <QHash>
#include <QWidget>

class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace spotty {

class IPanelHost;
class JsonRateDelegate;
class JsonTreeModel;

/**
 * \class JsonTreeView
 * \brief Показывает spotty::JsonTreeModel деревом со значением и частотой.
 *
 * \par Два таймера, оба одиночные
 *
 * Модель испускает changed() на каждый документ — десятками раз в секунду, а на быстром
 * потоке и чаще. Вешать на этот сигнал перерисовку нельзя (записанная ловушка: так поток
 * интерфейса уходил в отрисовку целиком у плоттера). Поэтому строение дерева обновляется
 * не чаще #kStructureIntervalMs, а перерисовка идёт не чаще #kPaintIntervalMs. Таймеры
 * одиночные: когда данные не идут, ничего не тикает.
 *
 * \par Исключение: затухание догорает после остановки потока
 *
 * Частота падает к нулю сама по себе, формулой от текущего времени, — значит после
 * последнего документа картинку нужно ещё некоторое время обновлять, иначе числа замрут на
 * экране, хотя на самом деле уже уменьшились. Таймер перерисовки поэтому перевзводит себя,
 * пока среди видимых строк есть что-то живое, и останавливается, когда всё догорело.
 *
 * \par Отдельный виджет, а не часть панели
 *
 * Та же причина, что у spotty::PlotWidget: дерево может понадобиться и в отдельном окне, и
 * тогда его достаточно создать вторым экземпляром над той же моделью.
 */
class JsonTreeView : public QWidget
{
    Q_OBJECT

public:
    JsonTreeView(IPanelHost *panelHost, JsonTreeModel *model, QWidget *parent = nullptr);

    /// \brief Пересобрать цвета делегата под текущую тему.
    void applyTheme();

    /// \brief Показывать ли вспышку при изменении значения.
    void setFlashEnabled(bool enabled);

    /// \brief Сколько горит вспышка, мс.
    void setFlashDurationMs(int milliseconds);
    int flashDurationMs() const;

    /// \brief Прятать ли замершие строки, не удаляя их.
    void setHideStale(bool hide);
    bool hideStale() const { return m_hideStale; }

    /// \brief Раскрыть всё дерево.
    void expandTree();

    /**
     * \brief Свернуть всё дерево до корневых веток.
     *
     * Новые ветки после этого раскрываются по-прежнему: свёрнутость — состояние ветки, а
     * не режим показа, и удерживать её насильно значило бы прятать пришедшие данные.
     */
    void collapseTree();

    /// \brief Построить дерево заново по модели.
    void rebuild();

    /// \brief Строка под курсором либо -1; для контекстного меню панели.
    int nodeAt(const QPoint &viewportPosition) const;

    QTreeWidget *tree() const { return m_tree; }

private:
    /// \brief Как часто в дерево заносятся новые узлы и новые значения, мс.
    static constexpr int kStructureIntervalMs = 120;

    /// \brief Как часто перерисовывается картинка ради вспышек и затухания, мс.
    static constexpr int kPaintIntervalMs = 40;

    /// \brief Замедленный шаг затухания, когда поток уже встал, мс.
    static constexpr int kIdlePaintIntervalMs = 250;

    /// \brief Через сколько после последнего документа перерисовка замедляется, мс.
    static constexpr int kIdleAfterMs = 1000;

    /// \brief Завести таймер строения, если он ещё не идёт.
    void scheduleStructure();

    /// \brief Завести таймер перерисовки, если он ещё не идёт.
    void schedulePaint();

    /// \brief Занести в дерево появившиеся узлы и обновлённые значения.
    void syncStructure();

    /// \brief Перерисовать и решить, нужен ли следующий кадр.
    void paintTick();

    /// \brief Есть ли среди видимых строк то, что ещё меняется само по себе.
    bool animationAlive(qint64 nowNs) const;

    /// \brief Создать элемент для узла и всех его родителей, которых ещё нет.
    QTreeWidgetItem *ensureItem(int node);

    /// \brief Записать в элемент имя и значение узла.
    void fillItem(QTreeWidgetItem *item, int node);

    /// \brief Убрать элементы удалённых узлов.
    void removeNodes(const QList<int> &nodes);

    IPanelHost *m_host = nullptr;
    JsonTreeModel *m_model = nullptr;
    QTreeWidget *m_tree = nullptr;
    JsonRateDelegate *m_delegate = nullptr;

    /// \brief Индекс узла → его элемент. Индексы модели устойчивы, поэтому хэш переживает уборку.
    QHash<int, QTreeWidgetItem *> m_items;

    QTimer *m_structureTimer = nullptr;
    QTimer *m_paintTimer = nullptr;

    /// \brief Когда пришёл последний документ — по нему решается, замедлять ли затухание.
    qint64 m_lastDocumentNs = 0;

    bool m_hideStale = false;
};

} // namespace spotty
