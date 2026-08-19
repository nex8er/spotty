/**
 * \file TerminalBuffer.cpp
 * \brief Реализация spotty::TerminalBuffer.
 */
#include "TerminalBuffer.h"

namespace spotty {

namespace {

/// \brief Ширина табуляции в символах.
constexpr int kTabWidth = 8;

/// \brief Нижняя граница предела строк: буфер меньше этого бесполезен.
constexpr int kMinMaxLines = 100;

} // namespace

/**
 * \class TerminalBuffer::SinkAdapter
 * \brief Переходник между событиями разборщика ANSI и буфером.
 *
 * Отдельный класс, а не наследование TerminalBuffer от AnsiParser::Sink: приёмник —
 * деталь реализации, и выносить семь виртуальных методов в публичный интерфейс буфера
 * незачем.
 */
class TerminalBuffer::SinkAdapter : public AnsiParser::Sink
{
public:
    explicit SinkAdapter(TerminalBuffer &buffer)
        : m_buffer(buffer)
    {
    }

    void text(QStringView text, const TextStyle &style) override
    {
        m_buffer.writeText(text, style);
    }

    void lineFeed() override
    {
        // Строка закрывается, следующая создастся при первой же записи. Так завершающий
        // перевод строки не порождает лишнюю пустую строку в конце вывода.
        if (Line *line = m_buffer.currentLine()) {
            line->complete = true;
            Q_EMIT m_buffer.lineFinalized(m_buffer.nextLineNumber() - 1);
        }
        m_buffer.m_hasOpenLine = false;
        m_buffer.m_cursorColumn = 0;
    }

    void carriageReturn() override
    {
        // Курсор в начало строки без создания новой — на этом работают индикаторы
        // прогресса, перерисовывающие строку на месте.
        m_buffer.m_cursorColumn = 0;
    }

    void backspace() override
    {
        if (m_buffer.m_cursorColumn > 0)
            --m_buffer.m_cursorColumn;
    }

    void tab() override
    {
        const int next = ((m_buffer.m_cursorColumn / kTabWidth) + 1) * kTabWidth;
        const TextStyle style;
        m_buffer.writeText(QString(next - m_buffer.m_cursorColumn, u' '), style);
    }

    void eraseInLine(int mode) override
    {
        Line *line = m_buffer.currentLine();
        if (!line)
            return;

        switch (mode) {
        case 0: // От курсора до конца строки.
            if (m_buffer.m_cursorColumn < line->text.size()) {
                line->text.truncate(m_buffer.m_cursorColumn);
                m_buffer.rebuildRuns(*line, line->text.size(), {}, TextStyle{});
            }
            break;
        case 1: // От начала строки до курсора.
            m_buffer.rebuildRuns(*line, 0, QString(m_buffer.m_cursorColumn, u' '),
                                 TextStyle{});
            break;
        case 2: // Вся строка.
            line->text.clear();
            line->runs.clear();
            break;
        default:
            return;
        }
        Q_EMIT m_buffer.lineUpdated(m_buffer.nextLineNumber() - 1);
    }

    void eraseInDisplay(int mode) override
    {
        // Различать «вниз от курсора» и «вверх от курсора» негде: экранной модели нет,
        // есть журнал. Любую очистку экрана трактуем как очистку буфера — это то, чего
        // ждёт человек, увидевший `clear` от устройства.
        if (mode == 2 || mode == 3)
            m_buffer.clear();
    }

private:
    TerminalBuffer &m_buffer;
};

TerminalBuffer::TerminalBuffer(QObject *parent)
    : QObject(parent)
{
}

TerminalBuffer::~TerminalBuffer() = default;

void TerminalBuffer::setMaxLines(int maxLines)
{
    m_maxLines = qMax(kMinMaxLines, maxLines);
    trimToLimit();
}

void TerminalBuffer::setEncoding(QStringConverter::Encoding encoding)
{
    m_rxParser.setEncoding(encoding);
    m_txParser.setEncoding(encoding);
}

TerminalBuffer::Line *TerminalBuffer::currentLine()
{
    if (!m_hasOpenLine || m_lines.empty())
        return nullptr;
    return &m_lines.back();
}

const TerminalBuffer::Line *TerminalBuffer::line(qint64 lineNumber) const
{
    const qint64 index = lineNumber - m_firstLineNumber;
    if (index < 0 || index >= qint64(m_lines.size()))
        return nullptr;
    return &m_lines[size_t(index)];
}

void TerminalBuffer::append(const QByteArray &data, DataDirection direction,
                            qint64 monotonicNs, bool terminatesLine, quint8 source)
{
    if (data.isEmpty())
        return;

    // Смена направления закрывает текущую строку: иначе отправленное и принятое слиплись
    // бы в одну строку и метка направления перестала бы что-либо значить.
    if (m_hasOpenLine && (direction != m_currentDirection || source != m_currentSource)) {
        if (Line *line = currentLine()) {
            line->complete = true;
            Q_EMIT lineFinalized(nextLineNumber() - 1);
        }
        m_hasOpenLine = false;
        m_cursorColumn = 0;
    }
    m_currentDirection = direction;
    m_currentSource = source;

    if (direction == DataDirection::Rx)
        m_bytesReceived += data.size();
    else if (direction == DataDirection::Tx)
        m_bytesSent += data.size();

    const qint64 lineNumberBefore = nextLineNumber();
    m_currentMonotonicNs = monotonicNs;

    SinkAdapter sink(*this);
    AnsiParser &parser = direction == DataDirection::Tx ? m_txParser : m_rxParser;

    // Данные скармливаются разборщику отрезками по переводам строки, а не целиком.
    // Иначе исходные байты порции легли бы целиком в первую строку, и HEX-режим показывал
    // бы для неё содержимое всех последующих, а для них — пустоту.
    qsizetype start = 0;
    while (start < data.size()) {
        const qsizetype newline = data.indexOf('\n', start);
        const qsizetype end = newline >= 0 ? newline + 1 : data.size();
        const QByteArray segment = data.mid(start, end - start);

        // Строка создаётся не здесь, а лениво — при первой же записи текста.
        //
        // Иначе отрезок из одних управляющих последовательностей порождал бы пустую
        // строку: устройства сплошь и рядом шлют «...текст\r\n\033[0m», и сброс цвета,
        // попавший после перевода строки, добавлял бы в вывод пустую строку после каждой
        // осмысленной.
        if (m_hasOpenLine) {
            if (Line *line = currentLine())
                line->raw.append(segment);
        } else {
            // Байты придержим: их заберёт строка, которую создаст первая запись текста.
            // Если текста так и не будет, они достанутся следующей строке — управляющая
            // последовательность действует именно на неё.
            m_pendingRaw.append(segment);
        }

        parser.parse(segment, sink);
        start = end;
    }

    if (terminatesLine) {
        // Пакет мог состоять из одних управляющих байтов — разборщик их отбрасывает, и
        // текста не появилось. Строку всё равно нужно создать: иначе исходные байты
        // пропадут вместе с ней, и двоичная посылка исчезнет из вывода целиком, включая
        // HEX-режим.
        if (!m_hasOpenLine && !m_pendingRaw.isEmpty())
            startLine(direction, monotonicNs);

        if (Line *line = currentLine()) {
            line->complete = true;
            Q_EMIT lineFinalized(nextLineNumber() - 1);
        }
        m_hasOpenLine = false;
        m_cursorColumn = 0;
    }

    const qint64 added = nextLineNumber() - lineNumberBefore;
    if (added > 0)
        Q_EMIT linesAppended(lineNumberBefore, added);
    else
        Q_EMIT lineUpdated(nextLineNumber() - 1);

    trimToLimit();
    Q_EMIT statisticsChanged();
}

void TerminalBuffer::appendSystemMessage(const QString &message, quint8 source)
{
    if (m_hasOpenLine) {
        if (Line *line = currentLine()) {
            line->complete = true;
            Q_EMIT lineFinalized(nextLineNumber() - 1);
        }
        m_hasOpenLine = false;
        m_cursorColumn = 0;
    }

    const qint64 lineNumber = nextLineNumber();

    // Курсив принадлежит самим данным и сохраняется при копировании. Звёздочку рисует
    // TerminalView в колонке направления рядом с «>» и «<»: это метка представления,
    // а не часть сообщения программы.
    const QString text = message;
    TextStyle style;
    style.italic = true;

    Line line;
    line.text = text;
    line.runs.append(StyleRun{0, int(text.size()), style});
    line.wallClock = QDateTime::currentDateTime();
    line.direction = DataDirection::System;
    line.source = source;
    line.complete = true;
    m_lines.push_back(std::move(line));

    Q_EMIT linesAppended(lineNumber, 1);
    Q_EMIT lineFinalized(lineNumber);
    trimToLimit();
}

void TerminalBuffer::startLine(DataDirection direction, qint64 monotonicNs)
{
    Line line;
    line.monotonicNs = monotonicNs;
    line.wallClock = QDateTime::currentDateTime();
    line.direction = direction;
    line.source = m_currentSource;
    // Байты, придержанные до появления строки, принадлежат именно ей.
    line.raw = m_pendingRaw;
    m_pendingRaw.clear();
    m_lines.push_back(std::move(line));

    m_hasOpenLine = true;
    m_cursorColumn = 0;
}

void TerminalBuffer::writeText(QStringView text, const TextStyle &style)
{
    if (text.isEmpty())
        return;

    // Разборщик может выдать текст после перевода строки, когда открытой строки уже нет.
    if (!m_hasOpenLine)
        startLine(m_currentDirection, m_currentMonotonicNs);

    Line *line = currentLine();
    if (!line)
        return;

    if (m_cursorColumn == line->text.size()) {
        // Быстрый путь: дописывание в конец. Если оформление совпало с последним
        // отрезком, он просто удлиняется — иначе список отрезков рос бы на каждый вызов.
        line->text.append(text);
        if (!line->runs.isEmpty() && line->runs.last().style == style)
            line->runs.last().length += int(text.size());
        else
            line->runs.append(StyleRun{m_cursorColumn, int(text.size()), style});
        m_cursorColumn += int(text.size());
        return;
    }

    // Медленный путь: перезапись середины после возврата каретки.
    rebuildRuns(*line, m_cursorColumn, text, style);
    m_cursorColumn += int(text.size());
}

void TerminalBuffer::rebuildRuns(Line &line, int fromColumn, QStringView text,
                                 const TextStyle &style)
{
    // Разворачиваем отрезки в оформление на каждый символ, правим нужный участок и
    // сворачиваем обратно. Дорого, но случается только при перезаписи середины строки —
    // а это индикаторы прогресса, то есть короткие строки и десятки раз в секунду.
    QList<TextStyle> perChar;
    perChar.reserve(qMax(line.text.size(), fromColumn + int(text.size())));
    for (const StyleRun &run : std::as_const(line.runs)) {
        for (int i = 0; i < run.length; ++i)
            perChar.append(run.style);
    }
    perChar.resize(line.text.size());

    // Пропуск позиций дополняется пробелами: курсор мог уйти вправо за конец строки.
    if (fromColumn > line.text.size()) {
        const int padding = fromColumn - line.text.size();
        line.text.append(QString(padding, u' '));
        perChar.append(QList<TextStyle>(padding, TextStyle{}));
    }

    const int required = fromColumn + int(text.size());
    if (required > line.text.size()) {
        line.text.resize(required);
        perChar.resize(required);
    }

    for (int i = 0; i < text.size(); ++i) {
        line.text[fromColumn + i] = text[i];
        perChar[fromColumn + i] = style;
    }

    line.runs.clear();
    for (int i = 0; i < perChar.size(); ++i) {
        if (!line.runs.isEmpty() && line.runs.last().style == perChar.at(i))
            ++line.runs.last().length;
        else
            line.runs.append(StyleRun{i, 1, perChar.at(i)});
    }
}

void TerminalBuffer::trimToLimit()
{
    if (qint64(m_lines.size()) <= m_maxLines)
        return;

    const qint64 excess = qint64(m_lines.size()) - m_maxLines;
    for (qint64 i = 0; i < excess; ++i)
        m_lines.pop_front();
    m_firstLineNumber += excess;

    Q_EMIT trimmed(m_firstLineNumber);
}

void TerminalBuffer::clear()
{
    // Сквозная нумерация продолжается: номер строки должен оставаться уникальным за всё
    // время работы, иначе сохранённая где-то позиция после очистки укажет на чужую строку.
    m_firstLineNumber = nextLineNumber();
    m_lines.clear();
    m_hasOpenLine = false;
    m_cursorColumn = 0;
    m_pendingRaw.clear();
    m_rxParser.reset();
    m_txParser.reset();

    Q_EMIT cleared();
}

void TerminalBuffer::resetStatistics()
{
    m_bytesReceived = 0;
    m_bytesSent = 0;
    Q_EMIT statisticsChanged();
}

} // namespace spotty
