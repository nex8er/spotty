/**
 * \file TerminalBuffer.h
 * \brief Кольцевой буфер строк терминала.
 */
#pragma once

#include "AnsiParser.h"

#include <spotty/api/DataDirection.h>
#include <spotty/data/TextStyle.h>

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <deque>

namespace spotty {

/**
 * \class TerminalBuffer
 * \brief Хранилище выведенных строк с оформлением, метками времени и направлением.
 *
 * \par Абсолютная нумерация строк
 *
 * Строки нумеруются сквозным счётчиком, который никогда не сбрасывается, а не индексом в
 * массиве. Это принципиально: буфер ограничен по размеру и подрезается спереди, а
 * подрезка при индексной адресации сдвинула бы все номера — и позиция прокрутки,
 * выделение и результаты поиска молча уехали бы на несколько строк. При сквозной
 * нумерации номер строки означает одно и то же всё время её жизни.
 *
 * Доступ идёт через line(), которая возвращает `nullptr` для строки, уже вытесненной из
 * буфера.
 *
 * \par Оформление
 *
 * Строка хранит текст и список отрезков с одинаковым оформлением. Добавление в конец
 * строки — операция постоянного времени: если оформление совпало с последним отрезком,
 * он просто удлиняется. Перезапись середины строки (после возврата каретки) требует
 * перестроения списка, но случается редко и на коротких строках.
 */
class TerminalBuffer : public QObject
{
    Q_OBJECT

public:
    /**
     * \struct StyleRun
     * \brief Отрезок строки с одинаковым оформлением.
     */
    struct StyleRun
    {
        int start = 0;  ///< Позиция начала в TerminalBuffer::Line::text.
        int length = 0; ///< Длина в символах.
        TextStyle style;
    };

    /**
     * \struct Line
     * \brief Одна строка терминала.
     */
    struct Line
    {
        QString text;           ///< Текст без управляющих последовательностей.
        QList<StyleRun> runs;   ///< Отрезки оформления; покрывают весь text.
        QByteArray raw;         ///< Исходные байты — источник для HEX-режима.
        qint64 monotonicNs = 0; ///< Момент прихода по монотонным часам.
        QDateTime wallClock;    ///< Он же по календарным часам — для меток времени.
        DataDirection direction = DataDirection::Rx;

        /**
         * \brief Номер транспорта, от которого пришла строка.
         *
         * Ноль, пока транспорт один, — и тогда о нём ничего не сообщается. При двух
         * открытых интерфейсах строки идут в общий буфер вперемешку по времени, и без
         * пометки понять, кто что сказал, невозможно.
         */
        quint8 source = 0;

        /// \brief Строка завершена переводом строки или границей пакета.
        bool complete = false;
    };

    explicit TerminalBuffer(QObject *parent = nullptr);
    ~TerminalBuffer() override;

    /**
     * \brief Ограничение на число хранимых строк.
     *
     * При превышении самые старые строки вытесняются. Уменьшение предела подрезает буфер
     * немедленно.
     */
    void setMaxLines(int maxLines);
    int maxLines() const { return m_maxLines; }

    /// \brief Кодировка принимаемого текста.
    void setEncoding(QStringConverter::Encoding encoding);

    /**
     * \brief Добавить данные в буфер.
     * \param data Байты как пришли.
     * \param direction Откуда они.
     * \param monotonicNs Момент по монотонным часам.
     * \param terminatesLine Завершить строку после этих данных — так пакетизатор
     *        сообщает, что пакет кончился, даже если перевода строки в нём не было.
     * \param source Номер транспорта, которому принадлежат данные.
     *
     * Смена направления всегда завершает текущую строку: отправленное и принятое не
     * должны смешиваться в одной строке, иначе метка направления теряет смысл.
     */
    void append(const QByteArray &data, DataDirection direction, qint64 monotonicNs,
                bool terminatesLine = false, quint8 source = 0);

    /**
     * \brief Добавить сообщение самой программы отдельной строкой.
     *
     * Не проходит через разбор ANSI: текст задаём мы сами, и управляющих
     * последовательностей в нём быть не может.
     * \param source Номер транспорта, к которому относится сообщение.
     */
    void appendSystemMessage(const QString &message, quint8 source = 0);

    /// \brief Очистить буфер. Сквозная нумерация при этом продолжается.
    void clear();

    /// \brief Номер самой старой хранимой строки.
    qint64 firstLineNumber() const { return m_firstLineNumber; }

    /// \brief Номер, который получит следующая созданная строка.
    qint64 nextLineNumber() const { return m_firstLineNumber + qint64(m_lines.size()); }

    /// \brief Сколько строк сейчас в буфере.
    int lineCount() const { return int(m_lines.size()); }

    /**
     * \brief Строка по сквозному номеру.
     * \return `nullptr`, если строка уже вытеснена или ещё не создана.
     * \warning Указатель действителен до следующего изменения буфера.
     */
    const Line *line(qint64 lineNumber) const;

    /// \brief Суммарно принято байт с момента последней очистки счётчиков.
    qint64 bytesReceived() const { return m_bytesReceived; }

    /// \brief Суммарно отправлено байт.
    qint64 bytesSent() const { return m_bytesSent; }

    /// \brief Обнулить счётчики байтов.
    void resetStatistics();

Q_SIGNALS:
    /**
     * \brief Добавлены новые строки.
     * \param firstLineNumber Номер первой добавленной.
     * \param count Сколько добавлено.
     */
    void linesAppended(qint64 firstLineNumber, qint64 count);

    /**
     * \brief Изменено содержимое существующей строки.
     *
     * Возникает при возврате каретки и очистке строки — том, на чём работают индикаторы
     * прогресса, перерисовывающие строку на месте.
     */
    void lineUpdated(qint64 lineNumber);

    /// \brief Строка получила окончание и больше не будет дописываться.
    void lineFinalized(qint64 lineNumber);

    /// \brief Старые строки вытеснены; \p newFirstLineNumber — новая граница буфера.
    void trimmed(qint64 newFirstLineNumber);

    /// \brief Буфер очищен.
    void cleared();

    /// \brief Изменились счётчики принятых и отправленных байт.
    void statisticsChanged();

private:
    class SinkAdapter;
    friend class SinkAdapter;

    /// \brief Начать новую строку, завершив текущую.
    void startLine(DataDirection direction, qint64 monotonicNs);

    /// \brief Дописать текст в текущую строку с учётом положения курсора.
    void writeText(QStringView text, const TextStyle &style);

    /// \brief Пересчитать отрезки оформления после перезаписи середины строки.
    void rebuildRuns(Line &line, int fromColumn, QStringView text, const TextStyle &style);

    /// \brief Вытеснить лишние строки спереди.
    void trimToLimit();

    /// \brief Текущая незавершённая строка либо nullptr.
    Line *currentLine();

    std::deque<Line> m_lines;
    qint64 m_firstLineNumber = 0;
    int m_maxLines = 20000;

    /// \brief Положение курсора в текущей строке. Меняется возвратом каретки и забоем.
    int m_cursorColumn = 0;

    /// \brief Номер транспорта, которому принадлежит открытая или следующая строка.
    quint8 m_currentSource = 0;

    /// \brief Есть ли незавершённая строка, в которую пишем.
    bool m_hasOpenLine = false;

    DataDirection m_currentDirection = DataDirection::Rx;

    /// \brief Метка времени текущей порции — ею штампуются строки, начатые внутри разбора.
    qint64 m_currentMonotonicNs = 0;

    /**
     * \brief Байты, пришедшие до появления строки, которой они принадлежат.
     *
     * Строка создаётся лениво, при первой записи текста, поэтому исходные байты нужно
     * где-то придержать. Отрезок из одних управляющих последовательностей строки не
     * создаёт вовсе, и его байты достаются следующей строке — что и правильно, поскольку
     * последовательность действует именно на неё.
     */
    QByteArray m_pendingRaw;

    /**
     * \brief Отдельный разборщик на каждое направление.
     *
     * Оформление, установленное устройством, не должно протекать в отражённый вывод и
     * наоборот: это разные потоки, и цвет из середины строки прошивки не имеет отношения
     * к тому, что печатаем мы.
     */
    AnsiParser m_rxParser;
    AnsiParser m_txParser;

    qint64 m_bytesReceived = 0;
    qint64 m_bytesSent = 0;
};

} // namespace spotty
