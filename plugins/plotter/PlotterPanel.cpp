/**
 * \file PlotterPanel.cpp
 * \brief Реализация spotty::PlotterPanel.
 */
#include "PlotterPanel.h"

#include "PlotCanvas.h"
#include "PlotWidget.h"
#include <spotty/data/PlotFormat.h>
#include <spotty/data/PlotModel.h>
#include <spotty/data/PlotViewState.h>

#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
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
    ColumnVisible = 0, ///< Флажок «показывать».
    ColumnName,
    ColumnColor,
    ColumnMin,
    ColumnMax,
    ColumnAverage,
    ColumnCount,
};

/// \brief Квадратик цвета ряда. Значком, а не фоном ячейки: фон перебивает QSS.
QIcon colorSwatch(const QColor &color)
{
    constexpr int kSwatch = 14;

    QPixmap pixmap(kSwatch, kSwatch);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0x80, 0x80, 0x80), 1));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0.5, 0.5, kSwatch - 1, kSwatch - 1), 2, 2);

    return QIcon(pixmap);
}

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
    m_table->setHorizontalHeaderLabels({QString(), tr("Series"), tr("Colour"),
                                        tr("Min"), tr("Max"), tr("Avg")});
    // Растягивается только имя ряда; остальные колонки ужимаются под содержимое. Иначе
    // равные доли отдают под галочку и квадратик цвета столько же, сколько под число.
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
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

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column != ColumnColor || row >= m_model->seriesCount())
            return;
        const QColor chosen = QColorDialog::getColor(
            QColor::fromRgba(m_model->series(row).color), this, tr("Series colour"));
        // Отменённый диалог отдаёт недействительный цвет: перестраивать таблицу тогда не за
        // чем, а лишняя перестройка сбрасывает выделение строки под курсором.
        if (!chosen.isValid())
            return;
        m_model->setSeriesColor(row, chosen.rgba());
        rebuildTable();
    });

    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_populating || item->column() != ColumnVisible)
            return;
        m_model->setSeriesVisible(item->row(), item->checkState() == Qt::Checked);
    });

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

        auto *visible = new QTableWidgetItem;
        visible->setCheckState(series.visible ? Qt::Checked : Qt::Unchecked);
        visible->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ColumnVisible, visible);

        m_table->setItem(row, ColumnName, new QTableWidgetItem(series.name));

        auto *color = new QTableWidgetItem;
        color->setIcon(colorSwatch(QColor::fromRgba(series.color)));
        color->setToolTip(tr("Double-click to change"));
        m_table->setItem(row, ColumnColor, color);

        for (const int column : {ColumnMin, ColumnMax, ColumnAverage})
            m_table->setItem(row, column, new QTableWidgetItem);
    }

    m_populating = false;
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
        const double minimum = stats.finiteCount > 0 ? stats.minimum : qQNaN();
        const double maximum = stats.finiteCount > 0 ? stats.maximum : qQNaN();
        const double mean = stats.finiteCount > 0 ? stats.mean : qQNaN();

        m_table->item(row, ColumnMin)->setText(PlotFormat::number(minimum, 5));
        m_table->item(row, ColumnMax)->setText(PlotFormat::number(maximum, 5));
        m_table->item(row, ColumnAverage)->setText(PlotFormat::number(mean, 5));
    }
    m_populating = false;
}

} // namespace spotty
