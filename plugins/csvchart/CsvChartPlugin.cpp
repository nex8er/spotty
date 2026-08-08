/**
 * \file CsvChartPlugin.cpp
 * \brief Реализация spotty::CsvChartPlugin.
 */
#include "CsvChartPlugin.h"

#include "CsvChartOverlay.h"
#include "CsvChartPanel.h"
#include "CsvSeries.h"

#include <spotty/ui/MdiCodepoints.h>

namespace spotty {

CsvChartPlugin::CsvChartPlugin()
    : m_series(new CsvSeries(this))
{
}

CsvChartPlugin::~CsvChartPlugin() = default;

QList<PanelDescriptor> CsvChartPlugin::panels() const
{
    return {
        PanelDescriptor{
            .id = QStringLiteral("csvchart"),
            .title = tr("CSV chart"),
            .glyph = mdi::ChartLine,
            .placement = PanelPlacement::Rail,
            .order = 500,
        },
        PanelDescriptor{
            .id = QStringLiteral("csvchart.plot"),
            .title = tr("CSV plot"),
            .placement = PanelPlacement::Overlay,
            .order = 500,
            .anchor = PanelAnchor::Fill,
            // График только показывает и ни на что не нажимается. Без этого флага он
            // отнял бы у терминала выделение текста на всей своей площади.
            .mouseTransparent = true,
        },
    };
}

QWidget *CsvChartPlugin::createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
{
    // Подписка ставится один раз, при создании первой из двух панелей: хост один на
    // плагин, и второй connect() удваивал бы разбор каждой строки.
    if (!m_host) {
        m_host = host;
        m_nextLine = host->nextLineNumber();

        connect(host, &IPanelHost::terminalLinesAppended, this,
                [this](qint64 first, qint64 count) {
                    // Читаем от того места, где остановились, а не от first: строки могли
                    // появиться и до создания панели, а буфер мог подрезаться спереди.
                    m_nextLine = qMax(m_nextLine, m_host->firstLineNumber());
                    const qint64 end = first + count;

                    for (qint64 number = m_nextLine; number < end; ++number) {
                        TerminalLine line;
                        if (!m_host->line(number, &line))
                            continue;
                        if (line.direction != DataDirection::Rx)
                            continue; // Отправленное — не данные устройства.
                        m_series->feed(line.text.trimmed());
                    }
                    m_nextLine = end;
                });
    }

    if (panelId == QLatin1String("csvchart"))
        return new CsvChartPanel(host, m_series, parent);
    if (panelId == QLatin1String("csvchart.plot"))
        return new CsvChartOverlay(host, m_series, parent);
    return nullptr;
}

} // namespace spotty
