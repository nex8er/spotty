/**
 * \file MdiIcons.cpp
 * \brief Реализация spotty::MdiIcons.
 */
#include "MdiIcons.h"

#include "ThemeManager.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QLoggingCategory>
#include <QPainter>
#include <QPixmap>

namespace spotty {

/// \brief Категория журналирования: `spotty.icons`.
Q_LOGGING_CATEGORY(lcIcons, "spotty.icons")

namespace {

QString g_family;          ///< Имя семейства загруженного шрифта; пусто, если не загружен.
bool g_initialized = false; ///< Была ли попытка загрузки.

/// \brief Кэш готовых значков по ключу «глиф + цвет + размер».
QHash<QString, QIcon> g_cache;

/// \brief Построить ключ кэша.
QString cacheKey(char32_t codepoint, const QColor &color, int size)
{
    return QStringLiteral("%1/%2/%3")
        .arg(static_cast<uint>(codepoint))
        .arg(color.rgba())
        .arg(size);
}

/**
 * \brief Цвет из текущей темы.
 * \param muted Взять приглушённый цвет вместо основного.
 *
 * Запасные значения нужны для случая, когда ThemeManager ещё не создан, — например при
 * построении значка из статического инициализатора.
 */
QColor themeColor(bool muted)
{
    if (ThemeManager *theme = ThemeManager::instance())
        return muted ? theme->colors().textMuted : theme->colors().text;
    return muted ? QColor(0x88, 0x88, 0x88) : QColor(0xdd, 0xdd, 0xdd);
}

} // namespace

bool MdiIcons::initialize()
{
    if (g_initialized)
        return !g_family.isEmpty();
    g_initialized = true;

    const int id =
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/materialdesignicons-webfont.ttf"));
    if (id < 0) {
        qCWarning(lcIcons) << "icon font resource missing - continuing without icons";
        return false;
    }

    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        qCWarning(lcIcons) << "icon font contains no families";
        return false;
    }

    g_family = families.first();
    qCInfo(lcIcons) << "icon font:" << g_family;
    return true;
}

bool MdiIcons::isAvailable()
{
    return !g_family.isEmpty();
}

QFont MdiIcons::font(int pixelSize)
{
    QFont font(g_family);
    font.setPixelSize(pixelSize);
    // Глифы веб-шрифта нарисованы в квадратной ячейке em. Хинтинг сдвинул бы их с центра,
    // а сглаживание, наоборот, нужно: значки мелкие и состоят из тонких линий.
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QString MdiIcons::glyph(char32_t codepoint)
{
    return QString::fromUcs4(&codepoint, 1);
}

QIcon MdiIcons::icon(char32_t codepoint, const QColor &color, int size)
{
    if (g_family.isEmpty())
        return {};

    const QString key = cacheKey(codepoint, color, size);
    const auto cached = g_cache.constFind(key);
    if (cached != g_cache.constEnd())
        return *cached;

    // Отрисовка в физическом разрешении: на дисплее с высокой плотностью точек значок,
    // нарисованный в логических пикселях, выглядел бы размытым.
    const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(color);
    painter.setFont(font(size));
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, glyph(codepoint));
    painter.end();

    const QIcon result(pixmap);
    g_cache.insert(key, result);
    return result;
}

QIcon MdiIcons::icon(char32_t codepoint, int size)
{
    return icon(codepoint, themeColor(/*muted=*/false), size);
}

QIcon MdiIcons::mutedIcon(char32_t codepoint, int size)
{
    return icon(codepoint, themeColor(/*muted=*/true), size);
}

} // namespace spotty
