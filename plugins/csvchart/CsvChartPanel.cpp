/**
 * \file CsvChartPanel.cpp
 * \brief Реализация spotty::CsvChartPanel.
 */
#include "CsvChartPanel.h"

#include "CsvSeries.h"

#include <spotty/ui/IPanelHost.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr auto kKeySeparator = "separator";
constexpr auto kKeyPoints = "points";
constexpr auto kKeyEnabled = "enabled";

constexpr int kDefaultPoints = 200;

} // namespace

CsvChartPanel::CsvChartPanel(IPanelHost *panelHost, CsvSeries *series, QWidget *parent)
    : PanelWidget(panelHost, parent)
    , m_series(series)
{
    setPanelTitle(tr("CSV chart"));
    QVBoxLayout *layout = content();

    auto *hint = new QLabel(tr("Plots numeric lines from the output, such as "
                               "\"12.5,3,-7\", over the terminal."),
                            this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *form = new QFormLayout;

    m_separator = new QComboBox(this);
    m_separator->addItem(tr("Comma"), QStringLiteral(","));
    m_separator->addItem(tr("Semicolon"), QStringLiteral(";"));
    m_separator->addItem(tr("Tab"), QStringLiteral("\t"));
    m_separator->addItem(tr("Space"), QStringLiteral(" "));
    form->addRow(tr("Separator"), m_separator);

    m_points = new QSpinBox(this);
    m_points->setRange(10, 10000);
    m_points->setValue(kDefaultPoints);
    m_points->setSuffix(tr(" points"));
    form->addRow(tr("Window"), m_points);

    layout->addLayout(form);

    m_enabled = new QCheckBox(tr("Show the chart"), this);
    layout->addWidget(m_enabled);

    m_clear = new QPushButton(tr("Clear data"), this);
    layout->addWidget(m_clear);

    m_stats = new QLabel(this);
    m_stats->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(m_stats);
    layout->addStretch(1);

    connect(m_separator, &QComboBox::currentIndexChanged, this, &CsvChartPanel::commit);
    connect(m_points, &QSpinBox::valueChanged, this, &CsvChartPanel::commit);
    connect(m_enabled, &QCheckBox::toggled, this, &CsvChartPanel::commit);
    connect(m_clear, &QPushButton::clicked, this, [this] { m_series->clear(); });
    connect(m_series, &CsvSeries::changed, this, &CsvChartPanel::updateStats);

    reloadFromSettings();
}

void CsvChartPanel::reloadFromSettings()
{
    m_populating = true;

    const QString separator =
        host()->value(QLatin1String(kKeySeparator), QStringLiteral(",")).toString();
    const int index = m_separator->findData(separator);
    m_separator->setCurrentIndex(index >= 0 ? index : 0);

    m_points->setValue(host()->value(QLatin1String(kKeyPoints), kDefaultPoints).toInt());
    m_enabled->setChecked(host()->value(QLatin1String(kKeyEnabled), true).toBool());

    m_populating = false;

    // Настройки применяются и при чтении: модель создана плагином и о них ещё не знает.
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
        host()->setValue(QLatin1String(kKeyEnabled), m_enabled->isChecked());
    }

    updateStats();
}

void CsvChartPanel::settingsReset()
{
    reloadFromSettings();
}

void CsvChartPanel::updateStats()
{
    const int series = m_series->seriesCount();
    if (series == 0) {
        m_stats->setText(tr("No numeric lines yet."));
        return;
    }
    m_stats->setText(tr("%n series", nullptr, series)
                     + QStringLiteral(", ")
                     + tr("%n point(s)", nullptr, int(m_series->values(0).size())));
}

} // namespace spotty
