/**
 * \file JsonTreePlugin.cpp
 * \brief Реализация spotty::JsonTreePlugin.
 */
#include "JsonTreePlugin.h"

#include "JsonTreePanel.h"

#include <spotty/data/JsonTreeModel.h>
#include <spotty/ui/MdiCodepoints.h>

namespace spotty {

namespace {

// Ключи настроек; должны совпадать со схемой в settingsSchema().
constexpr auto kKeyArrayKey = "arrayKey";
constexpr auto kKeyMaxNodes = "maxNodes";
constexpr auto kKeyMaxDepth = "maxDepth";
constexpr auto kKeyMaxChildren = "maxChildren";
constexpr auto kKeyPendingLines = "maxPendingLines";
constexpr auto kKeyPendingTimeout = "pendingTimeoutMs";

} // namespace

JsonTreePlugin::JsonTreePlugin()
    : m_model(new JsonTreeModel(this))
{
}

JsonTreePlugin::~JsonTreePlugin() = default;

QList<PanelDescriptor> JsonTreePlugin::panels() const
{
    return {
        PanelDescriptor{
            .id = QStringLiteral("jsontree"),
            .title = tr("JSON"),
            .glyph = mdi::CodeJson,
            .placement = PanelPlacement::Rail,
            .order = 550,
        },
    };
}

QWidget *JsonTreePlugin::createPanel(const QString &panelId, IPanelHost *host, QWidget *parent)
{
    // Подписка ставится один раз, а не при каждом создании панели: хост один на плагин, и
    // повторный connect() удваивал бы разбор каждой строки.
    if (!m_host) {
        m_host = host;
        m_nextLine = host->nextLineNumber();
        applySettings();

        // Настройки читает плагин: модель живёт дольше панели и разбирает поток, даже пока
        // панель ни разу не открывали.
        connect(host, &IPanelHost::settingsReset, this, &JsonTreePlugin::applySettings);

        connect(host, &IPanelHost::terminalLinesAppended, this,
                [this](qint64 first, qint64 count) {
                    // Читаем от того места, где остановились, а не от first: строки могли
                    // появиться и до создания панели, а буфер мог подрезаться спереди.
                    m_nextLine = qMax(m_nextLine, m_host->firstLineNumber());
                    const qint64 end = first + count;

                    while (m_nextLine < end) {
                        TerminalLine line;
                        if (!m_host->line(m_nextLine, &line)) {
                            ++m_nextLine; // Строка уже вытеснена из буфера — не дождаться.
                            continue;
                        }
                        // Источники вроде RTT режут строку на части не по границам данных.
                        // Недописанную строку нельзя ни разбирать сейчас (половина документа
                        // — это мусор), ни считать пройденной: останавливаемся на ней и
                        // перечитываем при следующем сигнале, когда она достроится.
                        if (!line.complete)
                            break;
                        // Пауза относится к разбору, а не к терминалу: устройство и журнал
                        // продолжают работать. Номер строки всё равно продвигаем, иначе
                        // после снятия паузы в дерево хлынул бы весь пропущенный поток.
                        if (line.direction == DataDirection::Rx && !m_paused) {
                            if (const auto document =
                                    m_framer.feed(line.text, line.monotonicNs)) {
                                m_model->feed(*document, line.monotonicNs);
                            }
                        }
                        ++m_nextLine;
                    }
                });
    }

    if (panelId != QLatin1String("jsontree"))
        return nullptr;

    auto *panel = new JsonTreePanel(host, m_model, &m_framer, parent);
    connect(panel, &JsonTreePanel::pauseChanged, this, [this](bool paused) {
        m_paused = paused;
    });
    return panel;
}

void JsonTreePlugin::applySettings()
{
    if (!m_host)
        return;

    m_model->setIdentityKey(m_host->value(QLatin1String(kKeyArrayKey)).toString());
    m_model->setMaxNodes(m_host->value(QLatin1String(kKeyMaxNodes),
                                       JsonTreeModel::kDefaultMaxNodes).toInt());
    m_model->setMaxDepth(m_host->value(QLatin1String(kKeyMaxDepth),
                                       JsonTreeModel::kDefaultMaxDepth).toInt());
    m_model->setMaxChildren(m_host->value(QLatin1String(kKeyMaxChildren),
                                          JsonTreeModel::kDefaultMaxChildren).toInt());

    m_framer.setMaxPendingLines(m_host->value(QLatin1String(kKeyPendingLines), 200).toInt());
    m_framer.setPendingTimeoutMs(
        m_host->value(QLatin1String(kKeyPendingTimeout), 2000).toInt());
}

SettingsSchema JsonTreePlugin::settingsSchema() const
{
    SettingsSchema schema;

    schema.add(SettingsField{
        .key = QStringLiteral("arrayKey"),
        .label = tr("Array identity field"),
        .group = tr("Data"),
        .type = SettingsField::Text,
        .defaultValue = QString(),
        .hint = tr("Objects inside an array are told apart by this field, for example id. "
                   "Leave it empty to collapse the array into a single branch showing the "
                   "last element's values."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("maxNodes"),
        .label = tr("Field limit"),
        .group = tr("Data"),
        .type = SettingsField::Integer,
        .defaultValue = JsonTreeModel::kDefaultMaxNodes,
        .minimum = 500,
        .maximum = 50000,
        .hint = tr("Once reached, new fields are ignored and the ones already there keep "
                   "updating. A stream that uses random ids will hit this quickly."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("maxDepth"),
        .label = tr("Maximum depth"),
        .group = tr("Data"),
        .type = SettingsField::Integer,
        .defaultValue = JsonTreeModel::kDefaultMaxDepth,
        .minimum = 1,
        .maximum = 32,
        .hint = tr("Deeper subtrees are shown collapsed into a single value."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("maxChildren"),
        .label = tr("Children per branch"),
        .group = tr("Data"),
        .type = SettingsField::Integer,
        .defaultValue = JsonTreeModel::kDefaultMaxChildren,
        .minimum = 16,
        .maximum = 8192,
    });

    schema.add(SettingsField{
        .key = QStringLiteral("maxPendingLines"),
        .label = tr("Multi-line JSON limit"),
        .group = tr("Framing"),
        .type = SettingsField::Integer,
        .defaultValue = 200,
        .minimum = 2,
        .maximum = 5000,
        .suffix = tr("lines"),
        .hint = tr("How many lines one pretty-printed document may span."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("pendingTimeoutMs"),
        .label = tr("Give up on unclosed JSON after"),
        .group = tr("Framing"),
        .type = SettingsField::Integer,
        .defaultValue = 2000,
        .minimum = 100,
        .maximum = 60000,
        .suffix = tr("ms"),
        .hint = tr("A lost closing brace would otherwise swallow everything that follows."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("flashOnChange"),
        .label = tr("Flash a field when its value changes"),
        .group = tr("View"),
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("The panel has a button for this too, and the same button sets the "
                   "duration by right-click."),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("flashMs"),
        .label = tr("Flash duration"),
        .group = tr("View"),
        .type = SettingsField::Integer,
        .defaultValue = 400,
        .minimum = 60,
        .maximum = 2000,
        .suffix = tr("ms"),
    });

    return schema;
}

} // namespace spotty
