/**
 * \file PlotterPanel.cpp
 * \brief Реализация spotty::PlotterPanel.
 */
#include "PlotterPanel.h"

#include "PlotCanvas.h"
#include <spotty/data/PlotFormat.h>
#include <spotty/data/PlotModel.h>

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

PlotterPanel::PlotterPanel(IPanelHost *panelHost, PlotModel *model, QWidget *parent)
    : PanelWidget(panelHost, parent)
    , m_model(model)
{
    setPanelTitle(tr("Plotter"));
    QVBoxLayout *layout = content();

    auto *hint = new QLabel(tr("Plots numeric lines from the output, such as \"12.5,3,-7\"."),
                            this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // График прямо в панели: узкий, но отвечает на вопрос «как это выглядит» без единого
    // лишнего действия. Для настоящего разглядывания есть отдельное окно.
    m_chart = new PlotCanvas(panelHost, m_model, this);
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

    connect(m_separator, &QComboBox::currentIndexChanged, this, &PlotterPanel::commit);
    connect(m_points, &QSpinBox::valueChanged, this, &PlotterPanel::commit);
    connect(m_xAxis, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_populating)
            m_model->setXAxisSeries(m_xAxis->currentData().toInt());
    });

    connect(m_pause, &QPushButton::toggled, m_chart, &PlotCanvas::setPaused);
    // Пауза переключается и двойным щелчком по полю графика — кнопка обязана это
    // отразить, иначе она показывала бы одно, а график делал другое.
    connect(m_chart, &PlotCanvas::pausedChanged, this, [this](bool paused) {
        const QSignalBlocker blocker(m_pause);
        m_pause->setChecked(paused);
    });

    connect(windowButton, &QPushButton::clicked, this, &PlotterPanel::openInWindow);
    connect(csvButton, &QToolButton::clicked, this, &PlotterPanel::exportCsv);
    connect(pngButton, &QToolButton::clicked, this, &PlotterPanel::exportImage);
    connect(clearButton, &QToolButton::clicked, this, [this] { m_model->clearSamples(); });

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
    const int index = m_separator->findData(separator);
    m_separator->setCurrentIndex(index >= 0 ? index : 0);
    m_points->setValue(host()->value(QLatin1String(kKeyPoints), kDefaultPoints).toInt());

    m_populating = false;
    commit();
}

void PlotterPanel::commit()
{
    const QString separator = m_separator->currentData().toString();
    m_model->setSeparator(separator.isEmpty() ? u',' : separator.at(0));
    m_model->setCapacity(m_points->value());

    if (!m_populating) {
        host()->setValue(QLatin1String(kKeySeparator), separator);
        host()->setValue(QLatin1String(kKeyPoints), m_points->value());
    }
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

    // Список колонок для оси X перестраивается вместе с таблицей: пункты в нём — те же
    // ряды, и держать два независимых списка одного и того же нельзя.
    const int previous = m_xAxis->currentData().isValid() ? m_xAxis->currentData().toInt() : -1;
    m_xAxis->clear();
    m_xAxis->addItem(tr("Time"), -1);
    for (int row = 0; row < m_model->seriesCount(); ++row)
        m_xAxis->addItem(m_model->series(row).name, row);
    m_xAxis->setCurrentIndex(qMax(0, m_xAxis->findData(previous)));

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

void PlotterPanel::openInWindow()
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
    m_window->setWindowTitle(tr("Spotty — plotter"));
    m_window->resize(720, 420);

    auto *layout = new QVBoxLayout(m_window);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new PlotCanvas(host(), m_model, m_window));

    connect(m_window, &QObject::destroyed, this, [this] { m_window = nullptr; });
    m_window->show();
}

void PlotterPanel::exportCsv()
{
    const QString data = m_model->toCsv();
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

void PlotterPanel::exportImage()
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
