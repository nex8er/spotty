/**
 * \file AppSettings.cpp
 * \brief Реализация spotty::AppSettings.
 */
#include "AppSettings.h"

#include "Paths.h"
#include "SettingsStore.h"

#include <QVariantMap>

namespace spotty {

namespace {

// Ключи объявлены здесь и только здесь. Любое второе место, знающее эти строки,
// рано или поздно разойдётся с первым.
constexpr auto kLanguage = "general/language";
constexpr auto kTheme = "appearance/theme";
constexpr auto kAutoOpen = "session/autoOpen";
constexpr auto kSingleInstance = "general/singleInstance";

constexpr auto kFontFamily = "terminal/fontFamily";
constexpr auto kFontSize = "terminal/fontSize";
constexpr auto kMaxLines = "terminal/maxLines";
constexpr auto kShowTimestamps = "terminal/timestamps";
constexpr auto kRelativeTimestamps = "terminal/relativeTimestamps";
constexpr auto kTimestampFormat = "terminal/timestampFormat";
constexpr auto kShowDirection = "terminal/showDirection";
constexpr auto kLocalEcho = "terminal/localEcho";
constexpr auto kHexBytesPerRow = "terminal/hexBytesPerRow";
constexpr auto kViewMode = "terminal/viewMode";
constexpr auto kEncoding = "terminal/encoding";
constexpr auto kAnsiPalette = "terminal/ansiPalette";

constexpr auto kSendFormat = "send/format";
constexpr auto kSendTermination = "send/termination";
constexpr auto kHistorySize = "send/historySize";


constexpr auto kPacketizerMode = "data/packetizerMode";
constexpr auto kPacketizerTimeout = "data/packetizerTimeoutMs";
constexpr auto kPacketizerDelimiter = "data/packetizerDelimiterHex";
constexpr auto kPacketizerLength = "data/packetizerFixedLength";

constexpr auto kShortcuts = "shortcuts";

} // namespace

AppSettings AppSettings::load(const SettingsStore &store)
{
    AppSettings settings;

    settings.language = store.value(QLatin1String(kLanguage), settings.language).toString();
    settings.theme = store.value(QLatin1String(kTheme), settings.theme).toString();
    settings.autoOpenLastInterface =
        store.value(QLatin1String(kAutoOpen), settings.autoOpenLastInterface).toBool();
    settings.singleInstance =
        store.value(QLatin1String(kSingleInstance), settings.singleInstance).toBool();

    settings.fontFamily = store.value(QLatin1String(kFontFamily)).toString();
    settings.fontSize = store.value(QLatin1String(kFontSize), settings.fontSize).toInt();
    settings.maxLines = store.value(QLatin1String(kMaxLines), settings.maxLines).toInt();
    settings.showTimestamps =
        store.value(QLatin1String(kShowTimestamps), settings.showTimestamps).toBool();
    settings.relativeTimestamps =
        store.value(QLatin1String(kRelativeTimestamps), settings.relativeTimestamps).toBool();
    settings.timestampFormat =
        store.value(QLatin1String(kTimestampFormat), settings.timestampFormat).toString();
    settings.showDirection =
        store.value(QLatin1String(kShowDirection), settings.showDirection).toBool();
    settings.localEcho = store.value(QLatin1String(kLocalEcho), settings.localEcho).toBool();
    settings.hexBytesPerRow =
        store.value(QLatin1String(kHexBytesPerRow), settings.hexBytesPerRow).toInt();
    settings.viewMode = store.value(QLatin1String(kViewMode), settings.viewMode).toString();
    settings.encoding = store.value(QLatin1String(kEncoding), settings.encoding).toString();
    settings.ansiPalette = store.value(QLatin1String(kAnsiPalette)).toStringList();

    settings.sendFormat = store.value(QLatin1String(kSendFormat), settings.sendFormat).toInt();
    settings.sendTermination =
        store.value(QLatin1String(kSendTermination), settings.sendTermination).toInt();
    settings.historySize =
        store.value(QLatin1String(kHistorySize), settings.historySize).toInt();


    settings.packetizerMode =
        store.value(QLatin1String(kPacketizerMode), settings.packetizerMode).toInt();
    settings.packetizerTimeoutMs =
        store.value(QLatin1String(kPacketizerTimeout), settings.packetizerTimeoutMs).toInt();
    settings.packetizerDelimiterHex =
        store.value(QLatin1String(kPacketizerDelimiter), settings.packetizerDelimiterHex)
            .toString();
    settings.packetizerFixedLength =
        store.value(QLatin1String(kPacketizerLength), settings.packetizerFixedLength).toInt();

    const QVariantMap shortcuts = store.value(QLatin1String(kShortcuts)).toMap();
    for (auto it = shortcuts.constBegin(); it != shortcuts.constEnd(); ++it)
        settings.shortcuts.insert(it.key(), it.value().toString());

    return settings;
}

void AppSettings::save(SettingsStore &store) const
{
    store.setValue(QLatin1String(kLanguage), language);
    store.setValue(QLatin1String(kTheme), theme);
    store.setValue(QLatin1String(kAutoOpen), autoOpenLastInterface);
    store.setValue(QLatin1String(kSingleInstance), singleInstance);

    store.setValue(QLatin1String(kFontFamily), fontFamily);
    store.setValue(QLatin1String(kFontSize), fontSize);
    store.setValue(QLatin1String(kMaxLines), maxLines);
    store.setValue(QLatin1String(kShowTimestamps), showTimestamps);
    store.setValue(QLatin1String(kRelativeTimestamps), relativeTimestamps);
    store.setValue(QLatin1String(kTimestampFormat), timestampFormat);
    store.setValue(QLatin1String(kShowDirection), showDirection);
    store.setValue(QLatin1String(kLocalEcho), localEcho);
    store.setValue(QLatin1String(kHexBytesPerRow), hexBytesPerRow);
    store.setValue(QLatin1String(kViewMode), viewMode);
    store.setValue(QLatin1String(kEncoding), encoding);
    store.setValue(QLatin1String(kAnsiPalette), ansiPalette);

    store.setValue(QLatin1String(kSendFormat), sendFormat);
    store.setValue(QLatin1String(kSendTermination), sendTermination);
    store.setValue(QLatin1String(kHistorySize), historySize);


    store.setValue(QLatin1String(kPacketizerMode), packetizerMode);
    store.setValue(QLatin1String(kPacketizerTimeout), packetizerTimeoutMs);
    store.setValue(QLatin1String(kPacketizerDelimiter), packetizerDelimiterHex);
    store.setValue(QLatin1String(kPacketizerLength), packetizerFixedLength);

    QVariantMap shortcutMap;
    for (auto it = shortcuts.constBegin(); it != shortcuts.constEnd(); ++it)
        shortcutMap.insert(it.key(), it.value());
    store.setValue(QLatin1String(kShortcuts), shortcutMap);
}

} // namespace spotty
