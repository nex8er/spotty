/**
 * \file KnownHosts.h
 * \brief Отпечатки ключей ssh-хостов, которым Spotty уже доверился.
 */
#pragma once

#include <QMutex>
#include "TabFileStore.h"

namespace spotty::tcptunnel {

/**
 * \class KnownHosts
 * \brief Хранилище «хост:порт → отпечаток ключа» в файле каталога данных плагина.
 *
 * Своё, а не `~/.ssh/known_hosts`: писать в чужой файл из прикладной программы без
 * спроса нельзя, а читать его Libssh2Session умеет и сама (ключи, уже принятые обычным ssh,
 * Spotty принимает без вопросов).
 *
 * \par Проверка ключа — единственное, что отличает вход по ssh от отправки пароля первому
 *      встречному
 *
 * Без неё любой посредник между Spotty и платой получил бы пароль root. Поэтому неизвестный
 * ключ принимается только по явной просьбе пользователя, а изменившийся — не принимается
 * вовсе: понять, взломан ли хост или переустановлена система, может только человек, и решение
 * «забыть старый ключ» — отдельное явное действие (forget()).
 *
 * Потокобезопасен: проверяет рабочий поток, а забывает поток интерфейса.
 */
class KnownHosts : private TabFileStore
{
public:
    enum class Match {
        Unknown,   ///< Этот хост ещё не видели.
        Same,      ///< Видели, ключ тот же.
        Different, ///< Видели, ключ другой.
    };

    /// \param filePath Файл хранилища; пустой путь — хранить только в памяти (до закрытия программы).
    explicit KnownHosts(QString filePath = {});

    Match check(const QString &host, int port, const QString &fingerprint) const;

    /// \brief Запомнить ключ. Уже известный хост получает новый отпечаток.
    bool remember(const QString &host, int port, const QString &fingerprint);

    /// \brief Забыть ключ хоста. \return `true`, если он был.
    bool forget(const QString &host, int port);

    /// \brief Сохранённый отпечаток; пусто, если хост неизвестен.
    QString fingerprint(const QString &host, int port) const;

    /// \brief Задать файл хранилища и прочитать его. Зовётся, когда ядро сообщило каталог данных.
    using TabFileStore::setFile;

private:
    static QString keyFor(const QString &host, int port);
};

} // namespace spotty::tcptunnel
