/**
 * \file CsvChartPlugin.cpp
 * \brief Реализация spotty::CsvChartPlugin.
 */
#include "CsvChartPlugin.h"

#include "CsvChartPanel.h"
#include "CsvChartView.h"
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
    // Одна панель вместо двух. Слоя поверх терминала больше нет: текст под кривой читать
    // трудно, а сама кривая теряется в тексте — страдали оба ради экономии места,
    // которой никто не просил. График живёт в своей панели, а для разглядывания
    // открывается отдельным окном.
    return {
        PanelDescriptor{
            .id = QStringLiteral("csvchart"),
            .title = tr("Chart"),
            .glyph = mdi::ChartLine,
            .placement = PanelPlacement::Rail,
            .order = 500,
        },
        // Большой график на месте терминала. Скрыт по умолчанию: показывается
        // переключателем режима области вывода, где и появляется его пункт.
        PanelDescriptor{
            .id = QStringLiteral("csvchart.plot"),
            .title = tr("Chart"),
            .placement = PanelPlacement::Splitter,
            .order = 500,
            .side = PanelSide::Below,
            .preferredSize = 320,
            .visibleByDefault = false,
        },
    };
}

QWidget *CsvChartPlugin::createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
{
    // Подписка ставится один раз, а не при каждом создании панели: хост один на плагин,
    // и повторный connect() удваивал бы разбор каждой строки.
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
                        // Отметка времени идёт вместе со строкой: точки приходят
                        // неравномерно, и равноотстоящая ось искажает форму сигнала.
                        m_series->feed(line.text.trimmed(), line.monotonicNs);
                    }
                    m_nextLine = end;
                });
    }

    if (panelId == QLatin1String("csvchart"))
        return new CsvChartPanel(host, m_series, parent);
    if (panelId == QLatin1String("csvchart.plot"))
        return new CsvChartView(host, m_series, parent);
    return nullptr;
}

} // namespace spotty
