/**
 * \file TabFileStore.h
 * \brief Таблица «ключ → значение» в текстовом файле каталога данных плагина.
 */
#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSaveFile>
#include <QString>
#include <QTextStream>

#include <map>

namespace spotty::tcptunnel {

/**
 * \class TabFileStore
 * \brief Основа KnownHosts и HostNames: строки `ключ<TAB>значение`, запись целиком и атомарно.
 *
 * Потомки держат #m_mutex на время своих операций и зовут save() под ним. QSaveFile: оборванная
 * запись не должна оставить половину файла — потерянные записи означали бы повторный вопрос, а
 * порченые — ложную тревогу.
 */
class TabFileStore
{
public:
    /// \brief Задать файл хранилища и прочитать его. Пустой путь — хранить только в памяти.
    void setFile(const QString &filePath)
    {
        QMutexLocker lock(&m_mutex);
        m_filePath = filePath;
        m_entries.clear();
        QFile file(m_filePath);
        if (m_filePath.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            const int tab = line.indexOf(QLatin1Char('\t'));
            if (!line.startsWith(QLatin1Char('#')) && tab > 0)
                m_entries[line.left(tab)] = line.mid(tab + 1).trimmed();
        }
    }

protected:
    /// \param header Первая строка файла (комментарий для человека, который его откроет).
    explicit TabFileStore(QString header)
        : m_header(std::move(header))
    {
    }

    /// \brief Записать таблицу в файл. Вызывается под #m_mutex.
    bool save() const
    {
        if (m_filePath.isEmpty())
            return true;
        QDir().mkpath(QFileInfo(m_filePath).absolutePath());
        QSaveFile file(m_filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        QTextStream out(&file);
        out << "# " << m_header << '\n';
        for (const auto &entry : m_entries)
            out << entry.first << '\t' << entry.second << '\n';
        out.flush();
        return file.commit();
    }

    mutable QMutex m_mutex;
    std::map<QString, QString> m_entries;

private:
    QString m_filePath;
    QString m_header;
};

} // namespace spotty::tcptunnel
