/**
 * \file HistoryStore.h
 * \brief История отправленных строк с автодополнением.
 */
#pragma once

#include <QString>
#include <QStringList>

namespace spotty {

/**
 * \class HistoryStore
 * \brief История строки отправки, хранимая в файле.
 *
 * Ведётся в обычном текстовом файле по строке на запись — так её можно посмотреть и
 * поправить любым редактором.
 *
 * \par Повторы
 *
 * Повторная отправка той же команды поднимает её наверх, а не создаёт вторую запись.
 * Иначе после десяти проверок одной команды история состояла бы из десяти её копий и
 * перебор стрелкой вверх стал бы бесполезен.
 */
class HistoryStore
{
public:
    /**
     * \brief Конструктор.
     * \param filePath Путь к файлу истории.
     * \param maxEntries Сколько записей хранить; старые вытесняются.
     */
    explicit HistoryStore(QString filePath, int maxEntries = 500);

    /// \brief Прочитать файл. Отсутствие файла — не ошибка.
    bool load();

    /// \brief Записать файл.
    bool save();

    /**
     * \brief Добавить запись.
     *
     * Пустая строка игнорируется. Повтор существующей записи перемещает её в конец.
     */
    void append(const QString &entry);

    /// \brief Записи от старых к новым.
    const QStringList &entries() const { return m_entries; }

    void clear();

    /**
     * \brief Дополнить префикс по истории — для клавиши Tab.
     * \param prefix Что уже набрано.
     * \param matches Куда положить все подходящие записи. Может быть `nullptr`.
     * \return Наибольшее общее продолжение всех подходящих записей.
     *
     * Возвращается именно общий префикс, а не первое совпадение: дополнять до одного из
     * вариантов, когда их несколько, значит навязывать пользователю выбор, которого он не
     * делал. Список \p matches позволяет показать варианты рядом.
     *
     * Поиск идёт от новых записей к старым — свежая команда вероятнее нужной.
     */
    QString complete(const QString &prefix, QStringList *matches = nullptr) const;

private:
    QString m_filePath;
    int m_maxEntries;
    QStringList m_entries;
    bool m_dirty = false;
};

} // namespace spotty
