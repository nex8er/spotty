/**
 * \file FileUtils.cpp
 * \brief Реализация мелких файловых операций.
 */
#include <spotty/data/FileUtils.h>

#include <QDir>
#include <QLoggingCategory>

namespace spotty {

namespace {
Q_LOGGING_CATEGORY(lcFiles, "spotty.files")
}

bool ensureDir(const QString &path)
{
    if (QDir().mkpath(path))
        return true;
    qCWarning(lcFiles) << "cannot create directory" << path;
    return false;
}

} // namespace spotty
