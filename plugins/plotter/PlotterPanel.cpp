/**
 * \file PlotterPanel.cpp
 * \brief Реализация spotty::PlotterPanel.
 */
#include "PlotterPanel.h"

#include "PlotCanvas.h"
#include "PlotWidget.h"
#include "SeriesSwatchDelegate.h"
#include <spotty/data/PlotFormat.h>
#include <spotty/data/PlotModel.h>
#include <spotty/data/PlotFormat.h>
#include <spotty/data/PlotViewState.h>

#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QColorDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr auto kKeySeparator = "separator";
constexpr auto kKeyCapacity = "capacity";

/**
 * \brief Ёмкость буфера по умолчанию, отсчётов.
 *
 * Прежние двести были размером **всего** буфера: старше двухсотой точки данных просто не
 * существовало, и прокручивать было нечего. Теперь буфер и видимое окно — разные вещи:
 * здесь задают, сколько всего хранить, а сколько из этого видно, решают масштаб и
 * прокрутка. Пятьдесят тысяч отсчётов на шестнадцать колонок — около семи мегабайт.
 */
constexpr int kDefaultCapacity = SampleBuffer::kDefaultCapacity;

/**
 * \brief Колонки таблицы рядов.
 *
 * Последнего значения здесь нет намеренно: панель узкая, семь колонок в неё не влезали и
 * уезжали за край вместе с прокруткой. Текущее значение и так показывает перекрестие на
 * самом графике, а таблица отвечает на другой вопрос — «в каких пределах гуляет ряд».
 */
enum Column {
    ColumnSwatch = 0, ///< Цвет ряда и галочка видимости на нём же.
    ColumnName,
    ColumnMin,
    ColumnMax,
    ColumnAverage,
    ColumnCount,
};

/// \brief Колонки со статистикой — у них общее правило ширины и формата.
constexpr Column kStatColumns[] = {ColumnMin, ColumnMax, ColumnAverage};

} // namespace

PlotterPanel::PlotterPanel(IPanelHost *panelHost, PlotModel *model, PlotViewState *view,
                           QWidget *parent)
    : PanelWidget(panelHost, parent)
    , m_model(model)
    , m_view(view)
{
    setPanelTitle(tr("Plotter"));
    QVBoxLayout *layout = content();

    // Подсказка убрана: она объясняла очевидное каждый раз, а место в узкой панели
    // дороже. Что рисует плоттер, видно по самому плоттеру.

    // Плоттер целиком, вместе со своим рядом кнопок: тот же композит, что в полосе вместо
    // терминала и в отдельном окне.
    m_plot = new PlotWidget(panelHost, m_model, m_view, PlotWidget::Placement::Panel, this);
    m_plot->canvas()->setMinimumHeight(140);
    layout->addWidget(m_plot);

    auto *form = new QFormLayout;

    m_points = new QSpinBox(this);
    m_points->setRange(100, 1'000'000);
    m_points->setValue(kDefaultCapacity);
    m_points->setSuffix(tr(" samples"));
    m_points->setToolTip(tr("How many samples to keep. What part of them is on screen is "
                            "set by scrolling and zooming the plot itself."));
    form->addRow(tr("Buffer"), m_points);

    layout->addLayout(form);

    // Таблица рядов. Она же легенда: прежде кривые различались только цветом, что и WCAG
    // нарушает, и просто не позволяет понять, какая из них чья.
    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setHorizontalHeaderLabels(
        {QString(), tr("Series"), tr("Min"), tr("Max"), tr("Avg")});
    // Растягивается только имя ряда; остальные колонки ужимаются под содержимое. Иначе
    // равные доли отдают под галочку и квадратик цвета столько же, сколько под число.
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnSwatch,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Выделение нескольких строк — это второй механизм из запроса: две и более выделенные
    // строки сводятся на общую шкалу. Активный ряд задаётся отдельно, «текущей» строкой.
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Правится только имя ряда, и только по двойному щелчку: одиночный по первой колонке
    // переключает видимость, и режим «правка по клику» отнял бы это у пользователя.
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    m_swatch = new SeriesSwatchDelegate(m_table);
    m_table->setItemDelegateForColumn(ColumnSwatch, m_swatch);
    // Горизонтальная прокрутка запрещена: в узкой панели она появлялась всегда и прятала
    // половину колонок. Числа при нехватке места сокращаются многоточием — увидеть, что
    // значение не поместилось, лучше, чем не увидеть колонку вовсе.
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setTextElideMode(Qt::ElideRight);
    layout->addWidget(m_table, 1);

    connect(m_points, &QSpinBox::valueChanged, this, &PlotterPanel::commit);

    // Окно единственное на всю программу, и владеет им плагин: и миниатюра, и полоса
    // просят открыть его одним и тем же сигналом.
    connect(m_plot, &PlotWidget::openInWindowRequested,
            this, &PlotterPanel::openInWindowRequested);

    // Разделитель и выбор оси X правят меню под графиком. Сохраняем по сигналу настройки,
    // а не по changed(): тот приходит на каждый отсчёт, тысячами в секунду.
    connect(m_model, &PlotModel::configurationChanged, this, [this] {
        if (m_populating)
            return;
        host()->setValue(QLatin1String(kKeySeparator), QString(m_model->separator()));
    });
    // Одиночный таймер: когда данные не идут, ничего не тикает.
    m_statisticsTimer = new QTimer(this);
    m_statisticsTimer->setSingleShot(true);
    m_statisticsTimer->setInterval(kStatisticsIntervalMs);
    connect(m_statisticsTimer, &QTimer::timeout, this, &PlotterPanel::refreshStatistics);

    connect(m_model, &PlotModel::seriesAdded, this, &PlotterPanel::rebuildTable);
    connect(m_model, &PlotModel::changed, this, &PlotterPanel::scheduleStatistics);

    // Первая колонка: одиночный щелчок переключает видимость, двойной открывает цвет.
    connect(m_swatch, &SeriesSwatchDelegate::visibilityToggled, this, [this](int row) {
        if (row >= 0 && row < m_model->seriesCount())
            m_model->setSeriesVisible(row, !m_model->series(row).visible);
    });
    connect(m_swatch, &SeriesSwatchDelegate::colourRequested, this, &PlotterPanel::pickColour);

    // Правка имени: сигнал приходит и от нашей же перестройки таблицы, поэтому флаг
    // m_populating обязателен — иначе панель писала бы имя обратно в модель на каждый
    // пришедший отсчёт.
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_populating || item->column() != ColumnName)
            return;
        m_model->setSeriesName(item->row(), item->text());
    });

    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &PlotterPanel::showTableMenu);

    // Два независимых механизма из запроса, и Qt даёт под них два готовых канала:
    // «текущая» строка (рамка фокуса) — активный ряд, чья шкала подписана слева;
    // «выделенные» строки (заливка) — группа с общей шкалой. Спорить им не о чем.
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (m_populating)
            return;
        QList<int> rows;
        const QModelIndexList selected = m_table->selectionModel()->selectedRows();
        rows.reserve(selected.size());
        for (const QModelIndex &index : selected)
            rows.append(index.row());
        m_view->setSelectionGroup(rows);
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current) {
                if (!m_populating && current.isValid())
                    m_view->setActiveSeries(current.row());
            });

    // Клик по шкале на графике тоже назначает активный ряд — таблица обязана это
    // отразить, не трогая при этом выделение.
    connect(m_view, &PlotViewState::changed, this, &PlotterPanel::syncActiveRow);

    // Ширина колонок статистики пересчитывается только при изменении ширины, и никогда по
    // приходу данных: именно это держит число знаков неподвижным.
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this] { updateStatisticsWidth(); });

    reloadFromSettings();
    rebuildTable();
}

void PlotterPanel::reloadFromSettings()
{
    m_populating = true;

    const QString separator =
        host()->value(QLatin1String(kKeySeparator), QStringLiteral(",")).toString();
    m_model->setSeparator(separator.isEmpty() ? u',' : separator.at(0));
    m_points->setValue(host()->value(QLatin1String(kKeyCapacity), kDefaultCapacity).toInt());

    m_populating = false;
    commit();
}

void PlotterPanel::commit()
{
    m_model->setCapacity(m_points->value());

    // Разделитель и ось X теперь меняют меню под графиком, а не поля панели, поэтому
    // сохраняются они по сигналу модели, а не отсюда: см. подписку в конструкторе.
    if (!m_populating)
        host()->setValue(QLatin1String(kKeyCapacity), m_points->value());
}

void PlotterPanel::settingsReset()
{
    reloadFromSettings();
}

void PlotterPanel::rebuildTable()
{
    m_populating = true;

    m_table->setRowCount(m_model->seriesCount());
    for (int row = 0; row < m_model->seriesCount(); ++row) {
        const PlotSeries &series = m_model->series(row);

        auto *swatch = m_table->item(row, ColumnSwatch);
        if (!swatch) {
            swatch = new QTableWidgetItem;
            swatch->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_table->setItem(row, ColumnSwatch, swatch);
        }
        swatch->setData(Qt::CheckStateRole, series.visible ? Qt::Checked : Qt::Unchecked);
        swatch->setData(SeriesSwatchDelegate::kColorRole, series.color);
        swatch->setToolTip(tr("Click to show or hide, double-click to change the colour"));

        auto *name = m_table->item(row, ColumnName);
        if (!name) {
            name = new QTableWidgetItem;
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            m_table->setItem(row, ColumnName, name);
        }
        name->setText(series.name);
        name->setToolTip(tr("Double-click to rename"));

        for (const Column column : kStatColumns) {
            if (!m_table->item(row, column)) {
                auto *cell = new QTableWidgetItem;
                cell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                m_table->setItem(row, column, cell);
            }
        }
    }

    m_populating = false;
    updateStatisticsWidth();
    refreshStatistics();
}

void PlotterPanel::scheduleStatistics()
{
    if (!m_statisticsTimer->isActive())
        m_statisticsTimer->start();
}

void PlotterPanel::refreshStatistics()
{
    if (m_table->rowCount() != m_model->seriesCount()) {
        rebuildTable();
        return;
    }

    m_populating = true;
    for (int row = 0; row < m_model->seriesCount(); ++row) {
        // Одна сводка на три числа вместо трёх независимых обходов окна, как было раньше.
        // Пустая колонка отдаёт finiteCount == 0, и в ячейке появляется тире: ноль там
        // читался бы как измеренное значение.
        const SampleBuffer::ColumnStats stats = m_model->samples().stats(row);
        const bool any = stats.finiteCount > 0;

        m_table->item(row, ColumnMin)
            ->setText(PlotFormat::number(any ? stats.minimum : qQNaN(), m_statisticsDigits));
        m_table->item(row, ColumnMax)
            ->setText(PlotFormat::number(any ? stats.maximum : qQNaN(), m_statisticsDigits));
        m_table->item(row, ColumnAverage)
            ->setText(PlotFormat::number(any ? stats.mean : qQNaN(), m_statisticsDigits));
    }
    m_populating = false;
}

void PlotterPanel::updateStatisticsWidth()
{
    // Число знаков выводится из ширины колонки в знакоместах и **не зависит от значения**.
    // Это и есть требование владельца: пока идут данные, цифры в таблице не прыгают —
    // ширина меняется только тогда, когда панель тянут за край.
    const int zero = qMax(1, m_table->fontMetrics().horizontalAdvance(u'0'));
    int narrowest = m_table->columnWidth(ColumnMin);
    for (const Column column : kStatColumns)
        narrowest = qMin(narrowest, m_table->columnWidth(column));

    const int digits = PlotFormat::digitsForCharacters(narrowest / zero);
    if (digits == m_statisticsDigits)
        return;

    m_statisticsDigits = digits;
    refreshStatistics();
}

void PlotterPanel::syncActiveRow()
{
    const int active = m_view->activeSeries();
    if (active < 0 || active >= m_table->rowCount())
        return;
    if (m_table->currentRow() == active)
        return;

    // NoUpdate: «текущая» строка ставится, не трогая выделение. Qt различает эти два
    // состояния изначально — рамка фокуса против заливки, — и на этом различии и держатся
    // два независимых механизма: активная ось и группа с общей шкалой.
    const QSignalBlocker blocker(m_table->selectionModel());
    m_table->selectionModel()->setCurrentIndex(m_table->model()->index(active, ColumnName),
                                               QItemSelectionModel::NoUpdate);
}

void PlotterPanel::pickColour(int row)
{
    if (row < 0 || row >= m_model->seriesCount())
        return;

    const QColor chosen = QColorDialog::getColor(
        QColor::fromRgba(m_model->series(row).color), window(), tr("Series colour"));
    // Отменённый диалог отдаёт недействительный цвет: перестраивать таблицу тогда незачем.
    if (chosen.isValid())
        m_model->setSeriesColor(row, chosen.rgba());
}

void PlotterPanel::showTableMenu(const QPoint &at)
{
    const int row = m_table->rowAt(at.y());
    if (row < 0 || row >= m_model->seriesCount())
        return;

    QMenu menu(m_table);
    connect(menu.addAction(tr("Change colour…")), &QAction::triggered, this,
            [this, row] { pickColour(row); });
    connect(menu.addAction(tr("Rename…")), &QAction::triggered, this, [this, row] {
        m_table->editItem(m_table->item(row, ColumnName));
    });

    menu.addSeparator();

    // Пределы шкалы задаются на ряд и переживают всё остальное: их не перебивает ни
    // автомасштаб, ни общая шкала группы.
    const PlotSeries &series = m_model->series(row);
    QAction *limits = menu.addAction(series.hasCustomRange ? tr("Change scale limits…")
                                                           : tr("Set scale limits…"));
    connect(limits, &QAction::triggered, this, [this, row] { editRange(row); });

    if (series.hasCustomRange) {
        connect(menu.addAction(tr("Back to automatic scale")), &QAction::triggered, this,
                [this, row] { m_model->setSeriesRange(row, false, 0.0, 1.0); });
    }

    menu.addSeparator();

    // Очистка одной колонки, а не всего: остальные ряды при этом продолжают идти.
    connect(menu.addAction(tr("Clear this series only")), &QAction::triggered, this,
            [this, row] { m_model->clearColumn(row); });

    menu.exec(m_table->viewport()->mapToGlobal(at));
}

void PlotterPanel::editRange(int row)
{
    if (row < 0 || row >= m_model->seriesCount())
        return;

    const PlotSeries &series = m_model->series(row);
    const SampleBuffer::ColumnStats stats = m_model->samples().stats(row);

    // Начальные значения — нынешние пределы, а при их отсутствии измеренные: так диалог
    // отвечает на вопрос «а какие они сейчас» ещё до того, как его зададут.
    const double presetMin = series.hasCustomRange ? series.customMinimum
                                                   : (stats.finiteCount > 0 ? stats.minimum : 0.0);
    const double presetMax = series.hasCustomRange ? series.customMaximum
                                                   : (stats.finiteCount > 0 ? stats.maximum : 1.0);

    bool ok = false;
    const double minimum = QInputDialog::getDouble(
        window(), tr("Scale limits"), tr("Minimum for %1:").arg(series.name), presetMin,
        -1e12, 1e12, 6, &ok);
    if (!ok)
        return;

    const double maximum = QInputDialog::getDouble(
        window(), tr("Scale limits"), tr("Maximum for %1:").arg(series.name), presetMax,
        -1e12, 1e12, 6, &ok);
    if (!ok)
        return;

    m_model->setSeriesRange(row, true, minimum, maximum);
}

} // namespace spotty
