/**
 * \file CsvChartPanel.cpp
 * \brief Реализация spotty::CsvChartPanel.
 */
#include "CsvChartPanel.h"

#include "CsvChartView.h"
#include "CsvSeries.h"

#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr auto kKeySeparator = "separator";
constexpr auto kKeyPoints = "points";

constexpr int kDefaultPoints = 200;

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

CsvChartPanel::CsvChartPanel(IPanelHost *panelHost, CsvSeries *series, QWidget *parent)
    : PanelWidget(panelHost, parent)
    , m_series(series)
{
    setPanelTitle(tr("Chart"));
    QVBoxLayout *layout = content();

    auto *hint = new QLabel(tr("Plots numeric lines from the output, such as \"12.5,3,-7\"."),
                            this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // График прямо в панели: узкий, но отвечает на вопрос «как это выглядит» без единого
    // лишнего действия. Для настоящего разглядывания есть отдельное окно.
    m_chart = new CsvChartView(panelHost, m_series, this);
    m_chart->setMinimumHeight(140);
    layout->addWidget(m_chart);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(4);

    m_pause = new QPushButton(tr("Pause"), this);
    m_pause->setCheckable(true);
    m_pause->setToolTip(tr("Freezes the picture, not the data: collecting continues."));
    buttonRow->addWidget(m_pause);

    auto *windowButton = new QPushButton(tr("Open in window"), this);
    buttonRow->addWidget(windowButton);
    layout->addLayout(buttonRow);

    auto *form = new QFormLayout;

    m_separator = new QComboBox(this);
    m_separator->addItem(tr("Comma"), QStringLiteral(","));
    m_separator->addItem(tr("Semicolon"), QStringLiteral(";"));
    m_separator->addItem(tr("Tab"), QStringLiteral("\t"));
    m_separator->addItem(tr("Space"), QStringLiteral(" "));
    form->addRow(tr("Separator"), m_separator);

    m_points = new QSpinBox(this);
    m_points->setRange(10, 100000);
    m_points->setValue(kDefaultPoints);
    m_points->setSuffix(tr(" points"));
    form->addRow(tr("Window"), m_points);

    m_xAxis = new QComboBox(this);
    m_xAxis->setToolTip(tr("Which column supplies X. Time is honest when the device does "
                           "not send a coordinate of its own."));
    form->addRow(tr("X axis"), m_xAxis);

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

    // Кнопки значками, а не подписями: три подписи в узкой панели обрезались до
    // «грузить С» и «кранить Р», что хуже, чем совсем без слов. Смысл несут подсказки.
    auto *exportRow = new QHBoxLayout;
    exportRow->setSpacing(4);

    const auto makeTool = [this](char32_t glyph, const QString &tip) {
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setIcon(host()->icon(glyph, 18));
        button->setToolTip(tip);
        return button;
    };

    auto *csvButton = makeTool(mdi::FileExport, tr("Export CSV"));
    auto *pngButton = makeTool(mdi::ContentSave, tr("Save PNG"));
    auto *clearButton = makeTool(mdi::Broom, tr("Clear"));
    exportRow->addWidget(csvButton);
    exportRow->addWidget(pngButton);
    exportRow->addWidget(clearButton);
    exportRow->addStretch(1);
    layout->addLayout(exportRow);

    connect(m_separator, &QComboBox::currentIndexChanged, this, &CsvChartPanel::commit);
    connect(m_points, &QSpinBox::valueChanged, this, &CsvChartPanel::commit);
    connect(m_xAxis, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_populating)
            m_series->setXAxisSeries(m_xAxis->currentData().toInt());
    });

    connect(m_pause, &QPushButton::toggled, m_chart, &CsvChartView::setPaused);
    // Пауза переключается и двойным щелчком по полю графика — кнопка обязана это
    // отразить, иначе она показывала бы одно, а график делал другое.
    connect(m_chart, &CsvChartView::pausedChanged, this, [this](bool paused) {
        const QSignalBlocker blocker(m_pause);
        m_pause->setChecked(paused);
    });

    connect(windowButton, &QPushButton::clicked, this, &CsvChartPanel::openInWindow);
    connect(csvButton, &QToolButton::clicked, this, &CsvChartPanel::exportCsv);
    connect(pngButton, &QToolButton::clicked, this, &CsvChartPanel::exportImage);
    connect(clearButton, &QToolButton::clicked, this, [this] { m_series->clear(); });

    // Одиночный таймер: когда данные не идут, ничего не тикает.
    m_statisticsTimer = new QTimer(this);
    m_statisticsTimer->setSingleShot(true);
    m_statisticsTimer->setInterval(kStatisticsIntervalMs);
    connect(m_statisticsTimer, &QTimer::timeout, this, &CsvChartPanel::refreshStatistics);

    connect(m_series, &CsvSeries::seriesAdded, this, &CsvChartPanel::rebuildTable);
    connect(m_series, &CsvSeries::changed, this, &CsvChartPanel::scheduleStatistics);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column != ColumnColor || row >= m_series->seriesCount())
            return;
        const QColor chosen = QColorDialog::getColor(m_series->series(row).color, this,
                                                     tr("Series colour"));
        if (chosen.isValid())
            m_series->setSeriesColor(row, chosen);
        rebuildTable();
    });

    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_populating || item->column() != ColumnVisible)
            return;
        m_series->setSeriesVisible(item->row(), item->checkState() == Qt::Checked);
    });

    reloadFromSettings();
    rebuildTable();
}

void CsvChartPanel::reloadFromSettings()
{
    m_populating = true;

    const QString separator =
        host()->value(QLatin1String(kKeySeparator), QStringLiteral(",")).toString();
    const int index = m_separator->findData(separator);
    m_separator->setCurrentIndex(index >= 0 ? index : 0);
    m_points->setValue(host()->value(QLatin1String(kKeyPoints), kDefaultPoints).toInt());

    m_populating = false;
    commit();
}

void CsvChartPanel::commit()
{
    const QString separator = m_separator->currentData().toString();
    m_series->setSeparator(separator.isEmpty() ? u',' : separator.at(0));
    m_series->setCapacity(m_points->value());

    if (!m_populating) {
        host()->setValue(QLatin1String(kKeySeparator), separator);
        host()->setValue(QLatin1String(kKeyPoints), m_points->value());
    }
}

void CsvChartPanel::settingsReset()
{
    reloadFromSettings();
}

void CsvChartPanel::rebuildTable()
{
    m_populating = true;

    m_table->setRowCount(m_series->seriesCount());
    for (int row = 0; row < m_series->seriesCount(); ++row) {
        const CsvSeries::Series &series = m_series->series(row);

        auto *visible = new QTableWidgetItem;
        visible->setCheckState(series.visible ? Qt::Checked : Qt::Unchecked);
        visible->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ColumnVisible, visible);

        m_table->setItem(row, ColumnName, new QTableWidgetItem(series.name));

        auto *color = new QTableWidgetItem;
        color->setIcon(colorSwatch(series.color));
        color->setToolTip(tr("Double-click to change"));
        m_table->setItem(row, ColumnColor, color);

        for (const int column : {ColumnMin, ColumnMax, ColumnAverage})
            m_table->setItem(row, column, new QTableWidgetItem);
    }

    // Список колонок для оси X перестраивается вместе с таблицей: пункты в нём — те же
    // ряды, и держать два независимых списка одного и того же нельзя.
    const int previous = m_xAxis->currentData().isValid() ? m_xAxis->currentData().toInt() : -1;
    m_xAxis->clear();
    m_xAxis->addItem(tr("Time"), -1);
    for (int row = 0; row < m_series->seriesCount(); ++row)
        m_xAxis->addItem(m_series->series(row).name, row);
    m_xAxis->setCurrentIndex(qMax(0, m_xAxis->findData(previous)));

    m_populating = false;
    refreshStatistics();
}

void CsvChartPanel::scheduleStatistics()
{
    if (!m_statisticsTimer->isActive())
        m_statisticsTimer->start();
}

void CsvChartPanel::refreshStatistics()
{
    if (m_table->rowCount() != m_series->seriesCount()) {
        rebuildTable();
        return;
    }

    m_populating = true;
    for (int row = 0; row < m_series->seriesCount(); ++row) {
        const CsvSeries::Series &series = m_series->series(row);
        m_table->item(row, ColumnMin)
            ->setText(QString::number(m_series->minimumOf(row), 'g', 5));
        m_table->item(row, ColumnMax)
            ->setText(QString::number(m_series->maximumOf(row), 'g', 5));
        m_table->item(row, ColumnAverage)
            ->setText(QString::number(m_series->averageOf(row), 'g', 5));
    }
    m_populating = false;
}

void CsvChartPanel::openInWindow()
{
    if (m_window) {
        // Второе окно того же графика ничего не добавляет: поднимаем уже открытое.
        m_window->raise();
        m_window->activateWindow();
        return;
    }

    // Окно без родителя: с родителем оно всегда оставалось бы поверх главного, а график
    // затем и открывают отдельно, чтобы положить его рядом, а не сверху.
    m_window = new QWidget;
    m_window->setAttribute(Qt::WA_DeleteOnClose);
    m_window->setWindowTitle(tr("Spotty — chart"));
    m_window->resize(720, 420);

    auto *layout = new QVBoxLayout(m_window);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new CsvChartView(host(), m_series, m_window));

    connect(m_window, &QObject::destroyed, this, [this] { m_window = nullptr; });
    m_window->show();
}

void CsvChartPanel::exportCsv()
{
    const QString data = m_series->toCsv();
    if (data.isEmpty()) {
        host()->showStatusMessage(tr("There is nothing to export yet."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        window(), tr("Export chart data"),
        host()->documentsDir() + QStringLiteral("/chart.csv"), tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;

    // QSaveFile, а не QFile: прерванная запись не должна оставить обрезанный файл, из
    // которого потом строят выводы.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
        || file.write(data.toUtf8()) < 0 || !file.commit()) {
        host()->showStatusMessage(tr("Could not write %1").arg(path));
        return;
    }
    host()->showStatusMessage(tr("Exported to %1").arg(path));
}

void CsvChartPanel::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        window(), tr("Save chart image"),
        host()->documentsDir() + QStringLiteral("/chart.png"), tr("PNG images (*.png)"));
    if (path.isEmpty())
        return;

    if (!m_chart->saveImage(path))
        host()->showStatusMessage(tr("Could not write %1").arg(path));
    else
        host()->showStatusMessage(tr("Saved to %1").arg(path));
}

} // namespace spotty
