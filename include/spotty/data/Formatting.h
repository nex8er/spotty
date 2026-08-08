/**
 * \file Formatting.h
 * \brief Небольшие помощники форматирования, общие для UI.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>

#include <QCoreApplication>
#include <QString>

class QDateTime;

namespace spotty {

/**
 * \class Formatting
 * \brief Помощники форматирования для показа пользователю.
 *
 * Все методы статические; класс существует лишь как пространство имён с поддержкой tr().
 */
class SPOTTY_API_EXPORT Formatting
{
    Q_DECLARE_TR_FUNCTIONS(spotty::Formatting)

public:
    /**
     * \brief Огрублённое «как давно».
     * \param when Момент в прошлом. Недействительный QDateTime даёт пустую строку.
     * \return Строку вида «только что», «5 мин назад», «2 ч назад».
     *
     * \note Огрубление намеренное. Список интерфейсов отвечает этой строкой на вопрос
     *       «устройство давно тут или только что появилось», а тикающий счётчик секунд
     *       притягивал бы взгляд, не сообщая ничего сверх того.
     */
    static QString timeAgo(const QDateTime &when);

    /**
     * \brief Объём данных с двоичной приставкой.
     * \param bytes Число байт.
     * \return Строку вида `"1.4 KiB"`.
     */
    static QString byteCount(qint64 bytes);
};

} // namespace spotty
