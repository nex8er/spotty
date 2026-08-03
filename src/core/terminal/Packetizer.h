/**
 * \file Packetizer.h
 * \brief Правило разбиения потока байтов на строки терминала.
 */
#pragma once

#include <QByteArray>
#include <QList>

namespace spotty {

/**
 * \class Packetizer
 * \brief Разбивает непрерывный поток на порции, каждая из которых станет строкой.
 *
 * \par Зачем
 *
 * Поток из порта не обязан содержать переводы строк. Двоичный протокол без них слипается
 * в одну бесконечную строку, в которой невозможно ни разглядеть границы сообщений, ни
 * поставить осмысленную метку времени. Пакетизатор задаёт, что считать одним сообщением.
 *
 * \par Режимы
 *
 * - #Mode::Stream — не вмешиваться: границы задают переводы строк, как в обычном
 *   терминале. Значение по умолчанию и правильный выбор для текстового вывода.
 * - #Mode::InterByteTimeout — граница там, где в потоке возникла пауза. Так устроено
 *   большинство двоичных протоколов поверх UART, включая Modbus RTU.
 * - #Mode::Delimiter — граница по заданной последовательности байтов.
 * - #Mode::FixedLength — сообщения постоянной длины.
 *
 * \par Работа с таймаутом
 *
 * Класс не владеет таймером и не зависит от цикла событий: в режиме таймаута feed()
 * возвращает готовые пакеты, а незавершённый остаток забирает flush(), который вызывает
 * владелец по срабатыванию своего таймера. Так пакетизатор остаётся чистой функцией от
 * входных данных и проверяется без QApplication.
 */
class Packetizer
{
public:
    /// \brief Правило разбиения.
    enum class Mode {
        Stream,            ///< Не вмешиваться; границы задают переводы строк.
        InterByteTimeout,  ///< Граница по паузе в потоке.
        Delimiter,         ///< Граница по последовательности байтов.
        FixedLength,       ///< Сообщения постоянной длины.
    };

    /**
     * \struct Packet
     * \brief Порция данных с признаком завершённости.
     */
    struct Packet
    {
        QByteArray data;

        /**
         * \brief Данные завершают строку.
         *
         * В режиме #Mode::Stream всегда `false`: там границу определит перевод строки
         * внутри данных, а не пакетизатор.
         */
        bool terminatesLine = false;
    };

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    /// \brief Пауза, после которой пакет считается завершённым, мс.
    void setTimeoutMs(int timeoutMs);
    int timeoutMs() const { return m_timeoutMs; }

    /// \brief Разделитель для #Mode::Delimiter. Пустое значение отключает разбиение.
    void setDelimiter(const QByteArray &delimiter);
    QByteArray delimiter() const { return m_delimiter; }

    /// \brief Длина сообщения для #Mode::FixedLength.
    void setFixedLength(int length);
    int fixedLength() const { return m_fixedLength; }

    /**
     * \brief Обработать очередную порцию.
     * \param data Байты как пришли.
     * \param monotonicNs Момент прихода по монотонным часам.
     * \return Готовые пакеты; остаток сохраняется до следующего вызова или flush().
     */
    QList<Packet> feed(const QByteArray &data, qint64 monotonicNs);

    /**
     * \brief Забрать незавершённый остаток как готовый пакет.
     * \return Пустой пакет, если остатка нет.
     *
     * Вызывается владельцем по истечении паузы в режиме #Mode::InterByteTimeout и при
     * закрытии канала — чтобы последнее сообщение не потерялось.
     */
    Packet flush();

    /// \return `true`, если есть незавершённый остаток и имеет смысл ждать паузы.
    bool hasPending() const { return !m_pending.isEmpty(); }

    /// \brief Сбросить накопленный остаток. Вызывается при открытии канала.
    void reset();

private:
    Mode m_mode = Mode::Stream;
    int m_timeoutMs = 20;
    QByteArray m_delimiter = QByteArrayLiteral("\n");
    int m_fixedLength = 16;

    QByteArray m_pending;
    qint64 m_lastByteNs = 0;

    /**
     * \brief Была ли уже хоть одна порция после сброса.
     *
     * Отдельный признак, а не ноль в #m_lastByteNs: ноль — совершенно законная отметка
     * монотонных часов сразу после запуска, и первая же порция, пришедшая в этот момент,
     * навсегда отключала бы разбиение по паузе.
     */
    bool m_hasLastByte = false;
};

} // namespace spotty
