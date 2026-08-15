/**
 * \file SettingsMigration.cpp
 * \brief Реализация spotty::SettingsMigration.
 */
#include "SettingsMigration.h"

#include "SettingsStore.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QVariantMap>

namespace spotty {

namespace {

/// \name Прежний и нынешний идентификаторы плоттера
/// Панель звалась «CSV chart», пока не стала плоттером с шестью режимами показа.
/// @{
constexpr auto kChartPluginGroup = "plugins/csvchart";
constexpr auto kPlotterPluginGroup = "plugins/plotter";
constexpr auto kChartPanelId = "csvchart";
constexpr auto kPlotterPanelId = "plotter";
constexpr auto kChartStripId = "csvchart.plot";
constexpr auto kPlotterStripId = "plotter.plot";
/// @}

/// \name Ключи окна, хранящие идентификаторы панелей
/// @{
constexpr auto kKeyPanelId = "window/panelId";
constexpr auto kKeyViewStrip = "window/viewStrip";
/// @}

/// \brief Прежнее имя ключа ёмкости: «окно» стало отдельным от буфера понятием.
constexpr auto kChartPointsKey = "plugins/plotter/points";
constexpr auto kPlotterCapacityKey = "plugins/plotter/capacity";

/**
 * \brief Заменить значение ключа, если оно ровно \p from.
 *
 * Сверка именно по значению, а не по одному лишь наличию ключа: пользователь мог выбрать
 * полосу другого плагина, и переписать её на плоттер значило бы отобрать у него выбор.
 */
bool replaceValue(SettingsStore &store, const char *key, const char *from, const char *to)
{
    const QString name = QLatin1String(key);
    if (store.value(name).toString() != QLatin1String(from))
        return false;

    store.setValue(name, QString::fromLatin1(to));
    return true;
}

} // namespace

bool SettingsMigration::movePluginKeys(SettingsStore &store)
{
    // Макросы, поиск и журналирование стали плагинами, и их ключи переехали из корня
    // settings.json под plugins/<id>/. Без переноса пользователь после обновления получил
    // бы сброшенные правила подсветки и потерянный набор макросов — настройки, которые
    // задают один раз и рассчитывают, что они останутся.
    static const QList<QPair<QString, QString>> moved = {
        {QStringLiteral("macros/preset"), QStringLiteral("plugins/macros/preset")},
        {QStringLiteral("search/highlightRules"),
         QStringLiteral("plugins/search/highlightRules")},
        {QStringLiteral("search/regularExpression"),
         QStringLiteral("plugins/search/regularExpression")},
        {QStringLiteral("search/caseSensitive"),
         QStringLiteral("plugins/search/caseSensitive")},
        {QStringLiteral("search/wholeWords"), QStringLiteral("plugins/search/wholeWords")},
        {QStringLiteral("logging/directory"), QStringLiteral("plugins/logging/directory")},
        {QStringLiteral("logging/fileNameTemplate"),
         QStringLiteral("plugins/logging/fileNameTemplate")},
        {QStringLiteral("logging/filterAnsi"), QStringLiteral("plugins/logging/filterAnsi")},
        {QStringLiteral("logging/includeTx"), QStringLiteral("plugins/logging/includeTx")},
        {QStringLiteral("logging/autoStart"), QStringLiteral("plugins/logging/autoStart")},
    };

    bool changed = false;
    for (const auto &[from, to] : moved) {
        if (!store.contains(from))
            continue;
        // Только если на новом месте пусто: иначе повторный запуск затирал бы то, что
        // человек успел изменить уже в новой версии.
        if (!store.contains(to))
            store.setValue(to, store.value(from));
        store.remove(from);
        changed = true;
    }

    return changed;
}

bool SettingsMigration::renameChartToPlotter(SettingsStore &store)
{
    bool changed = false;

    // Поддерево целиком, а не поимённый список ключей: так переедут и те настройки, что
    // панель успела завести между версиями, — знать их наперёд перенос не обязан.
    const QVariantMap chart = store.group(QLatin1String(kChartPluginGroup));
    if (!chart.isEmpty()) {
        QVariantMap plotter = store.group(QLatin1String(kPlotterPluginGroup));
        for (auto it = chart.cbegin(); it != chart.cend(); ++it) {
            if (!plotter.contains(it.key()))
                plotter.insert(it.key(), it.value());
        }
        store.setGroup(QLatin1String(kPlotterPluginGroup), plotter);
        store.remove(QLatin1String(kChartPluginGroup));
        changed = true;
    }

    // «Window: N points» задавало размер самого буфера, а теперь буфер и видимое окно —
    // разные вещи. Смысл прежнего числа — ёмкость, под этим именем оно и живёт дальше.
    const QString points = QLatin1String(kChartPointsKey);
    if (store.contains(points)) {
        if (!store.contains(QLatin1String(kPlotterCapacityKey)))
            store.setValue(QLatin1String(kPlotterCapacityKey), store.value(points));
        store.remove(points);
        changed = true;
    }

    changed |= replaceValue(store, kKeyPanelId, kChartPanelId, kPlotterPanelId);
    changed |= replaceValue(store, kKeyViewStrip, kChartStripId, kPlotterStripId);

    return changed;
}

bool SettingsMigration::apply(SettingsStore &store)
{
    bool changed = movePluginKeys(store);
    changed |= renameChartToPlotter(store);

    if (changed)
        store.save();

    return changed;
}

} // namespace spotty
