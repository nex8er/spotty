/**
 * \file PlotWidget.cpp
 * \brief Реализация spotty::PlotWidget.
 */
#include "PlotWidget.h"

#include "PlotCanvas.h"

#include <spotty/data/PlotModel.h>
#include <spotty/data/PlotViewState.h>

#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QSaveFile>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr int kToolGlyphSize = 18;

} // namespace

PlotWidget::PlotWidget(IPanelHost *panelHost, PlotModel *model, PlotViewState *view,
                       Placement placement, QWidget *parent)
    : QWidget(parent)
    , m_host(panelHost)
    , m_model(model)
    , m_view(view)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_canvas = new PlotCanvas(panelHost, model, view, this);
    layout->addWidget(m_canvas, 1);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(4, 0, 4, 0);
    row->setSpacing(2);

    // Одна кнопка на пуск и паузу, а не две: состояний два, и они взаимоисключающие —
    // вторая кнопка всегда была бы неактивной половиной пары.
    m_playPause = makeButton(mdi::Pause, QString());
    connect(m_playPause, &QToolButton::clicked, this,
            [this] { m_view->setPaused(!m_view->paused()); });
    connect(m_view, &PlotViewState::pausedChanged, this, &PlotWidget::updatePlayPause);
    updatePlayPause();
    row->addWidget(m_playPause);

    m_follow = makeButton(mdi::ArrowCollapseRight,
                          tr("Jump to the newest data and keep following it"));
    m_follow->setCheckable(true);
    m_follow->setChecked(m_view->following());
    connect(m_follow, &QToolButton::toggled, m_view, &PlotViewState::setFollowing);
    connect(m_view, &PlotViewState::followingChanged, this, [this](bool on) {
        const QSignalBlocker blocker(m_follow);
        m_follow->setChecked(on);
    });
    row->addWidget(m_follow);

    row->addSpacing(8);

    QToolButton *separator = makeButton(mdi::FormatColumns, tr("Field separator"));
    connect(separator, &QToolButton::clicked, this, &PlotWidget::showSeparatorMenu);
    row->addWidget(separator);

    QToolButton *xAxis = makeButton(mdi::AxisYArrow, tr("What goes on the X axis"));
    connect(xAxis, &QToolButton::clicked, this, &PlotWidget::showXAxisMenu);
    row->addWidget(xAxis);

    row->addStretch(1);

    QToolButton *csv = makeButton(mdi::FileExport, tr("Export CSV"));
    connect(csv, &QToolButton::clicked, this, &PlotWidget::exportCsv);
    row->addWidget(csv);

    // Левый клик — в буфер обмена: чаще всего график нужен, чтобы тут же вставить его в
    // переписку. Файл остаётся под правым кликом, для тех случаев, когда он и нужен.
    QToolButton *shot = makeButton(mdi::MonitorScreenshot,
                                   tr("Copy the plot as an image. Right-click to save a file"));
    connect(shot, &QToolButton::clicked, this, &PlotWidget::copyImage);
    shot->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(shot, &QToolButton::customContextMenuRequested, this, [this, shot](const QPoint &at) {
        QMenu menu(shot);
        connect(menu.addAction(tr("Save to file…")), &QAction::triggered,
                this, &PlotWidget::saveImage);
        menu.exec(shot->mapToGlobal(at));
    });
    row->addWidget(shot);

    if (placement != Placement::Window) {
        QToolButton *window = makeButton(mdi::OpenInNew, tr("Open in a separate window"));
        connect(window, &QToolButton::clicked, this, &PlotWidget::openInWindowRequested);
        row->addWidget(window);
    }

    QToolButton *clear = makeButton(mdi::Broom, tr("Clear the collected data"));
    connect(clear, &QToolButton::clicked, m_model, &PlotModel::clearSamples);
    row->addWidget(clear);

    layout->addLayout(row);
}

QToolButton *PlotWidget::makeButton(char32_t glyph, const QString &tip)
{
    auto *button = new QToolButton(this);
    button->setAutoRaise(true);
    button->setIcon(m_host->icon(glyph, kToolGlyphSize));
    button->setToolTip(tip);
    button->setIconSize(QSize(kToolGlyphSize, kToolGlyphSize));
    return button;
}

void PlotWidget::updatePlayPause()
{
    const bool paused = m_view->paused();
    m_playPause->setIcon(m_host->icon(paused ? mdi::Play : mdi::Pause, kToolGlyphSize));
    m_playPause->setToolTip(paused ? tr("Resume drawing; data kept coming while paused")
                                   : tr("Freeze the picture, not the data"));
}

void PlotWidget::showSeparatorMenu()
{
    QMenu menu(this);
    auto *group = new QActionGroup(&menu);
    group->setExclusive(true);

    const QList<QPair<QString, QChar>> presets = {
        {tr("Comma (,)"), u','},
        {tr("Semicolon (;)"), u';'},
        {tr("Tab"), u'\t'},
        {tr("Space character"), u' '},
        {tr("Pipe (|)"), u'|'},
    };

    for (const auto &[label, character] : presets) {
        QAction *action = menu.addAction(label);
        action->setCheckable(true);
        action->setChecked(m_model->separator() == character);
        action->setActionGroup(group);
        connect(action, &QAction::triggered, this,
                [this, character] { m_model->setSeparator(character); });
    }

    menu.addSeparator();
    connect(menu.addAction(tr("Custom…")), &QAction::triggered, this, [this] {
        bool ok = false;
        const QString text = QInputDialog::getText(
            window(), tr("Field separator"), tr("Separator character:"), QLineEdit::Normal,
            QString(m_model->separator()), &ok);
        if (ok && !text.isEmpty())
            m_model->setSeparator(text.at(0));
    });

    menu.exec(QCursor::pos());
}

void PlotWidget::showXAxisMenu()
{
    QMenu menu(this);
    auto *group = new QActionGroup(&menu);
    group->setExclusive(true);

    const auto addItem = [&](const QString &label, int index) {
        QAction *action = menu.addAction(label);
        action->setCheckable(true);
        action->setChecked(m_model->xAxisSeries() == index);
        action->setActionGroup(group);
        connect(action, &QAction::triggered, this,
                [this, index] { m_model->setXAxisSeries(index); });
    };

    // Время честнее номера отсчёта и не требует от устройства ничего лишнего, поэтому оно
    // первым и по умолчанию.
    addItem(tr("Time"), -1);
    for (int i = 0; i < m_model->seriesCount(); ++i)
        addItem(m_model->series(i).name, i);

    menu.exec(QCursor::pos());
}

void PlotWidget::copyImage()
{
    QApplication::clipboard()->setPixmap(m_canvas->snapshot());
    m_host->showStatusMessage(tr("Plot copied to the clipboard"));
}

void PlotWidget::saveImage()
{
    const QString path = QFileDialog::getSaveFileName(
        window(), tr("Save plot"), m_host->documentsDir() + QStringLiteral("/plot.png"),
        tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;

    if (m_canvas->saveImage(path))
        m_host->showStatusMessage(tr("Saved %1").arg(path));
    else
        m_host->showStatusMessage(tr("Cannot save %1").arg(path));
}

void PlotWidget::exportCsv()
{
    const QString text = m_model->toCsv();
    if (text.isEmpty()) {
        m_host->showStatusMessage(tr("Nothing to export yet"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        window(), tr("Export CSV"), m_host->documentsDir() + QStringLiteral("/plot.csv"),
        tr("CSV file (*.csv)"));
    if (path.isEmpty())
        return;

    // QSaveFile: прерванная запись не оставит наполовину написанный файл на месте прежнего.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_host->showStatusMessage(tr("Cannot save %1").arg(path));
        return;
    }
    file.write(text.toUtf8());
    file.write("\n");
    if (file.commit())
        m_host->showStatusMessage(tr("Saved %1").arg(path));
    else
        m_host->showStatusMessage(tr("Cannot save %1").arg(path));
}

} // namespace spotty
