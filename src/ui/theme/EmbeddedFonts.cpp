/**
 * \file EmbeddedFonts.cpp
 * \brief Реализация spotty::EmbeddedFonts.
 */
#include "EmbeddedFonts.h"

#include <QFontDatabase>
#include <QLoggingCategory>
#include <QStringList>

namespace spotty {

namespace {

Q_LOGGING_CATEGORY(lcFonts, "spotty.fonts")

bool g_initialized = false;
QString g_monospaceFamily;

/**
 * \brief Загрузить один файл шрифта из ресурсов.
 * \return Семейство или пустая строка.
 *
 * Начертания одного семейства регистрируются по отдельности, но семейство у них общее,
 * поэтому достаточно запомнить его один раз — от обычного начертания.
 */
QString loadFont(const QString &path)
{
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0) {
        qCWarning(lcFonts) << "cannot load embedded font" << path;
        return {};
    }

    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        qCWarning(lcFonts) << "embedded font contains no families" << path;
        return {};
    }
    return families.first();
}

} // namespace

void EmbeddedFonts::initialize()
{
    if (g_initialized)
        return;
    g_initialized = true;

    g_monospaceFamily =
        loadFont(QStringLiteral(":/fonts/FiraCodeNerdFontMono-Regular.ttf"));

    // Полужирное начертание грузится следом и отдельным вызовом. Без него Qt синтезирует
    // жирный, размазывая контур, — а полужирный в выводе устройства не редкость: его
    // ставит любая последовательность SGR 1.
    loadFont(QStringLiteral(":/fonts/FiraCodeNerdFontMono-Bold.ttf"));

    if (!g_monospaceFamily.isEmpty())
        qCInfo(lcFonts) << "terminal font:" << g_monospaceFamily;
}

QString EmbeddedFonts::monospaceFamily()
{
    return g_monospaceFamily;
}

} // namespace spotty
