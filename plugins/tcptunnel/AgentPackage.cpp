/**
 * \file AgentPackage.cpp
 * \brief Реализация комплекта агента.
 */
#include "AgentPackage.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace spotty::tcptunnel {

QByteArray readAgentFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

AgentPackage::AgentPackage(QString binariesDir, QString filesDir)
    : m_binariesDir(std::move(binariesDir))
    , m_filesDir(std::move(filesDir))
{
}

QByteArray AgentPackage::binary(const QString &arch) const
{
    // Имя архитектуры идёт в путь, поэтому допустимы только строчные буквы, цифры и `_`.
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9_]+$"));
    return pattern.match(arch).hasMatch()
               ? readAgentFile(QDir(m_binariesDir).filePath(arch + QStringLiteral("/spotty-uartd")))
               : QByteArray{};
}

QString AgentPackage::version() const
{
    return QString::fromUtf8(readAgentFile(QDir(m_binariesDir).filePath(QStringLiteral("VERSION")))).trimmed();
}

QByteArray AgentPackage::file(const QString &name) const
{
    return name.contains(QLatin1Char('/')) ? QByteArray{} : readAgentFile(QDir(m_filesDir).filePath(name));
}

} // namespace spotty::tcptunnel
