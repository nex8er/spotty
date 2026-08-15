/**
 * \file PlotterPlugin.cpp
 * \brief Реализация spotty::PlotterPlugin.
 */
#include "PlotterPlugin.h"

#include "PlotterPanel.h"
#include "PlotCanvas.h"
#include <spotty/data/PlotModel.h>
#include <spotty/data/PlotViewState.h>

#include <spotty/ui/MdiCodepoints.h>

namespace spotty {

PlotterPlugin::PlotterPlugin()
    : m_model(new PlotModel(this))
    , m_view(new PlotViewState(this))
{
}

PlotterPlugin::~PlotterPlugin() = default;

QList<PanelDescriptor> PlotterPlugin::panels() const
{
    // Одна панель вместо двух. Слоя поверх терминала больше нет: текст под кривой читать
    // трудно, а сама кривая теряется в тексте — страдали оба ради экономии места,
    // которой никто не просил. График живёт в своей панели, а для разглядывания
    // открывается отдельным окном.
    return {
        PanelDescriptor{
            .id = QStringLiteral("plotter"),
            .title = tr("Plotter"),
            .glyph = mdi::ChartAreaspline,
            .placement = PanelPlacement::Rail,
            .order = 500,
        },
        // Большой график на месте терминала. Скрыт по умолчанию: показывается
        // переключателем режима области вывода, где и появляется его пункт.
        PanelDescriptor{
            .id = QStringLiteral("plotter.plot"),
            .title = tr("Plotter"),
            .placement = PanelPlacement::Splitter,
            .order = 500,
            .side = PanelSide::Below,
            .preferredSize = 320,
            .visibleByDefault = false,
        },
    };
}

QWidget *PlotterPlugin::createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
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

                    // Источники вроде RTT опрашиваются таймером и режут строку на части не
                    // по границам данных — CSV-строка нередко приходит двумя и более
                    // порциями. terminalLinesAppended сигналит только о рождении новой
                    // строки, а не о довершении уже открытой, поэтому недописанную строку
                    // нельзя ни разбирать сейчас (обрежется часть колонок, а иногда и
                    // число посередине), ни считать пройденной — m_nextLine останавливаем
                    // на ней и перечитываем при следующем сигнале, когда она достроится.
                    while (m_nextLine < end) {
                        TerminalLine line;
                        if (!m_host->line(m_nextLine, &line)) {
                            ++m_nextLine; // Строка уже вытеснена из буфера — не дождаться.
                            continue;
                        }
                        if (!line.complete)
                            break;
                        if (line.direction == DataDirection::Rx) {
                            // Отметка времени идёт вместе со строкой: точки приходят
                            // неравномерно, и равноотстоящая ось искажает форму сигнала.
                            m_model->feed(line.text.trimmed(), line.monotonicNs);
                        }
                        ++m_nextLine;
                    }
                });
    }

    if (panelId == QLatin1String("plotter"))
        return new PlotterPanel(host, m_model, m_view, parent);
    if (panelId == QLatin1String("plotter.plot"))
        return new PlotCanvas(host, m_model, m_view, parent);
    return nullptr;
}

} // namespace spotty
