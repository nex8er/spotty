/**
 * \file TestSupport.h
 * \brief Вспомогательные средства для тестов.
 */
#pragma once

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>

namespace spotty::test {

/**
 * \brief Крутить очередь событий, пока условие не станет истинным.
 * \param predicate Проверяемое условие.
 * \param timeoutMs Предел ожидания.
 * \return `true`, если условие выполнилось до истечения срока.
 *
 * Нужен там, где проверяемый код опирается на таймеры или отложенные вызовы. Ожидание с
 * условием, а не фиксированная пауза: пауза либо замедляет набор тестов, либо оказывается
 * недостаточной на нагруженной машине непрерывной интеграции — и тест начинает падать
 * через раз, что хуже, чем не иметь его вовсе.
 */
inline bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 2000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!predicate()) {
        if (deadline.hasExpired())
            return predicate();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

/**
 * \class TempDir
 * \brief Временный каталог, удаляемый вместе с объектом.
 *
 * Обёртка над QTemporaryDir, добавляющая удобное получение путей внутри и проверку
 * успешного создания прямо в конструкторе.
 */
class TempDir
{
public:
    TempDir()
    {
        // Провал здесь означает, что тест будет проверять что-то другое, а не то, что
        // задумано. Лучше сообщить об этом сразу.
        EXPECT_TRUE(m_dir.isValid()) << "cannot create a temporary directory";
    }

    /// \brief Путь к самому каталогу.
    QString path() const { return m_dir.path(); }

    /// \brief Путь к файлу внутри каталога.
    QString filePath(const QString &name) const { return QDir(m_dir.path()).filePath(name); }

private:
    QTemporaryDir m_dir;
};

} // namespace spotty::test
