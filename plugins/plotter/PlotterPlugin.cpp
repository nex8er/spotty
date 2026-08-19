/**
 * \file PlotterPlugin.cpp
 * \brief Реализация spotty::PlotterPlugin.
 */
#include "PlotterPlugin.h"

#include "PlotterPanel.h"
#include "PlotWidget.h"
#include <spotty/data/PlotModel.h>
#include <spotty/data/PlotViewState.h>

#include <spotty/ui/MdiCodepoints.h>

#include <QVBoxLayout>
#include <QWidget>

namespace spotty {

namespace {

constexpr int kDefaultBufferK = 50;
constexpr int kMinimumBufferK = 1;
constexpr int kMaximumBufferK = 100;

} // namespace

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
                        // Пауза относится к захвату графика, а не к терминалу: устройство
                        // и общий журнал продолжают работать, но точка, пришедшая во время
                        // разглядывания кривой, не должна позже внезапно попасть в образец.
                        // Номер строки всё равно продвигаем ниже, иначе после снятия паузы
                        // плоттер перечитает и нарисует весь пропущенный поток.
                        if (line.direction == DataDirection::Rx && !m_view->paused()) {
                            // Отметка времени идёт вместе со строкой: точки приходят
                            // неравномерно, и равноотстоящая ось искажает форму сигнала.
                            m_model->feed(line.text.trimmed(), line.monotonicNs);
                        }
                        ++m_nextLine;
                    }
                });
    }

    if (panelId == QLatin1String("plotter")) {
        auto *panel = new PlotterPanel(host, m_model, m_view, parent);
        connect(panel, &PlotterPanel::openInWindowRequested, this,
                [this, host] { openInWindow(host); });
        return panel;
    }
    if (panelId == QLatin1String("plotter.plot")) {
        // Полоса вместо терминала — тот же композит, что миниатюра и окно: холст плюс свой
        // ряд кнопок. Кнопки полосы, которые подхватывает главное окно, объявляет холст.
        auto *strip = new PlotWidget(host, m_model, m_view, PlotWidget::Placement::Strip,
                                     parent);
        connect(strip, &PlotWidget::openInWindowRequested, this,
                [this, host] { openInWindow(host); });
        return strip;
    }
    return nullptr;
}

SettingsSchema PlotterPlugin::settingsSchema() const
{
    SettingsSchema schema;
    schema.add(SettingsField{
        .key = QStringLiteral("separator"),
        .label = tr("Field separator"),
        .group = tr("Data"),
        .type = SettingsField::Text,
        .defaultValue = QStringLiteral(","),
        .hint = tr("The first character separates incoming values into fields."),
    });
    schema.add(SettingsField{
        .key = QStringLiteral("bufferK"),
        .label = tr("Buffer"),
        .group = tr("Data"),
        .type = SettingsField::Integer,
        .defaultValue = kDefaultBufferK,
        .minimum = kMinimumBufferK,
        .maximum = kMaximumBufferK,
        .suffix = QStringLiteral("K"),
        .hint = tr("How many thousands of samples to keep."),
    });
    schema.add(SettingsField{
        .key = QStringLiteral("showDelta"),
        .label = tr("Show delta"),
        .group = tr("Table"),
        .type = SettingsField::Toggle,
        .defaultValue = false,
        .hint = tr("Show the difference between the maximum and minimum values for every "
                   "series."),
    });
    return schema;
}

void PlotterPlugin::openInWindow(IPanelHost *host)
{
    // Окно ровно одно: второе показывало бы те же данные в том же состоянии вида и лишь
    // отнимало бы место. Повторное нажатие поднимает уже открытое.
    if (m_window) {
        m_window->raise();
        m_window->activateWindow();
        return;
    }

    // Без родителя и с WA_DeleteOnClose: окно живёт само по себе, чтобы не закрываться
    // вместе с панелью, которую пользователь может свернуть.
    m_window = new QWidget;
    m_window->setAttribute(Qt::WA_DeleteOnClose);
    m_window->setWindowTitle(tr("Spotty — plotter"));
    m_window->resize(820, 480);

    auto *layout = new QVBoxLayout(m_window);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(new PlotWidget(host, m_model, m_view, PlotWidget::Placement::Window,
                                     m_window));

    connect(m_window, &QObject::destroyed, this, [this] { m_window = nullptr; });
    m_window->show();
}

} // namespace spotty
