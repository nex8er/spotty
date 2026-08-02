/**
 * \file Paths.cpp
 * \brief Реализация spotty::Paths.
 */
#include "Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace spotty {

/// \brief Категория журналирования: `spotty.paths`.
Q_LOGGING_CATEGORY(lcPaths, "spotty.paths")

namespace {

/// \brief Имя файла-маркера переносного режима.
constexpr auto kPortableMarker = "spotty-portable.txt";

bool g_initialized = false; ///< Была ли уже вызвана Paths::initialize().
bool g_portable = false;    ///< Обнаружен ли маркер переносного режима.
QString g_configDir;        ///< Разрешённый каталог конфигурации.

} // namespace

QString Paths::applicationRootDir()
{
    const QString binDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    // Внутри бандла исполняемый файл лежит в Spotty.app/Contents/MacOS — это не то место,
    // куда пользователь положит маркер переносного режима. Он видит только сам .app,
    // поэтому ищем рядом с ним, поднявшись на три уровня.
    QDir dir(binDir);
    if (dir.absolutePath().endsWith(QLatin1String("/Contents/MacOS")) && dir.cdUp()
        && dir.cdUp() && dir.cdUp()) {
        return dir.absolutePath();
    }
#endif
    return binDir;
}

void Paths::initialize()
{
    if (g_initialized)
        return;
    g_initialized = true;

    const QString root = applicationRootDir();
    g_portable = QFileInfo::exists(QDir(root).filePath(QLatin1String(kPortableMarker)));

    g_configDir = g_portable
        ? QDir(root).filePath(QStringLiteral("config"))
        : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

    ensureDir(g_configDir);
    qCInfo(lcPaths) << "config directory:" << g_configDir << (g_portable ? "(portable)" : "");
}

bool Paths::isPortable()
{
    return g_portable;
}

QString Paths::configDir()
{
    Q_ASSERT_X(g_initialized, "Paths::configDir", "Paths::initialize() was not called");
    return g_configDir;
}

QString Paths::settingsFile()
{
    return QDir(configDir()).filePath(QStringLiteral("settings.json"));
}

QString Paths::interfacesFile()
{
    return QDir(configDir()).filePath(QStringLiteral("interfaces.json"));
}

QString Paths::historyFile()
{
    return QDir(configDir()).filePath(QStringLiteral("history.txt"));
}

QString Paths::macrosDir()
{
    return QDir(configDir()).filePath(QStringLiteral("macros"));
}

QString Paths::defaultLogDir()
{
    if (g_portable)
        return QDir(applicationRootDir()).filePath(QStringLiteral("logs"));

    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(documents).filePath(QStringLiteral("Spotty/logs"));
}

QStringList Paths::pluginDirs()
{
    QStringList dirs;

    const QString override =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("SPOTTY_PLUGIN_PATH"));
    if (!override.isEmpty()) {
#ifdef Q_OS_WIN
        constexpr auto separator = u';';
#else
        constexpr auto separator = u':';
#endif
        dirs += override.split(separator, Qt::SkipEmptyParts);
    }

    dirs += QDir(configDir()).filePath(QStringLiteral("plugins"));

    const QString binDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    dirs += QDir(binDir).filePath(QStringLiteral("../PlugIns/spotty"));
#endif
    dirs += QDir(binDir).filePath(QStringLiteral("plugins"));
    dirs += QDir(binDir).filePath(QStringLiteral("../lib/spotty/plugins"));

    // canonicalPath() возвращает пустую строку для несуществующего каталога — заодно
    // отсеиваем отсутствующие пути и схлопываем разные записи, ведущие в одно место
    // (например, через символическую ссылку), чтобы плагин не был найден дважды.
    QStringList existing;
    for (const QString &dir : std::as_const(dirs)) {
        const QString canonical = QDir(dir).canonicalPath();
        if (!canonical.isEmpty() && !existing.contains(canonical))
            existing.append(canonical);
    }
    return existing;
}

bool Paths::ensureDir(const QString &path)
{
    if (QDir().mkpath(path))
        return true;
    qCWarning(lcPaths) << "cannot create directory" << path;
    return false;
}

} // namespace spotty
