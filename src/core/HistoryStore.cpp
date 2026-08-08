/**
 * \file HistoryStore.cpp
 * \brief Реализация spotty::HistoryStore.
 */
#include "HistoryStore.h"

#include "settings/Paths.h"

#include <spotty/data/FileUtils.h>

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTextStream>

namespace spotty {

/// \brief Категория журналирования: `spotty.history`.
Q_LOGGING_CATEGORY(lcHistory, "spotty.history")

HistoryStore::HistoryStore(QString filePath, int maxEntries)
    : m_filePath(std::move(filePath))
    , m_maxEntries(qMax(1, maxEntries))
{
}

bool HistoryStore::load()
{
    m_entries.clear();

    QFile file(m_filePath);
    if (!file.exists())
        return true; // Первый запуск.

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcHistory) << "cannot read" << m_filePath << file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (!line.isEmpty())
            m_entries.append(line);
    }

    // Файл могли дописать руками сверх предела.
    while (m_entries.size() > m_maxEntries)
        m_entries.removeFirst();

    m_dirty = false;
    return true;
}

bool HistoryStore::save()
{
    if (!m_dirty)
        return true;

    if (!ensureDir(QFileInfo(m_filePath).absolutePath()))
        return false;

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(lcHistory) << "cannot write" << m_filePath << file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    for (const QString &entry : std::as_const(m_entries))
        stream << entry << '\n';
    stream.flush();

    if (!file.commit()) {
        qCWarning(lcHistory) << "cannot commit" << m_filePath << file.errorString();
        return false;
    }

    m_dirty = false;
    return true;
}

void HistoryStore::append(const QString &entry)
{
    if (entry.isEmpty())
        return;

    // Повтор поднимается наверх вместо создания дубликата: иначе после десяти проверок
    // одной команды история состояла бы из десяти её копий.
    m_entries.removeAll(entry);
    m_entries.append(entry);

    while (m_entries.size() > m_maxEntries)
        m_entries.removeFirst();

    m_dirty = true;
}

void HistoryStore::clear()
{
    if (m_entries.isEmpty())
        return;
    m_entries.clear();
    m_dirty = true;
}

QString HistoryStore::complete(const QString &prefix, QStringList *matches) const
{
    if (matches)
        matches->clear();
    if (prefix.isEmpty())
        return prefix;

    QStringList found;
    // От новых к старым: свежая команда вероятнее нужной.
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        const QString &entry = m_entries.at(i);
        if (entry.size() > prefix.size() && entry.startsWith(prefix))
            found.append(entry);
    }

    if (matches)
        *matches = found;

    if (found.isEmpty())
        return prefix;

    // Наибольшее общее продолжение. Дополнять до первого совпадения, когда их несколько,
    // значило бы навязать выбор, которого пользователь не делал.
    QString common = found.first();
    for (const QString &candidate : std::as_const(found)) {
        int shared = 0;
        const int limit = qMin(common.size(), candidate.size());
        while (shared < limit && common.at(shared) == candidate.at(shared))
            ++shared;
        common.truncate(shared);
        if (common.size() == prefix.size())
            break;
    }

    return common;
}

} // namespace spotty
