/**
 * \file TerminalView.cpp
 * \brief Реализация spotty::TerminalView.
 */
#include "TerminalView.h"

#include "theme/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStringList>
#include <QTimer>
#include <QToolTip>

#include <algorithm>

namespace spotty {

namespace {

/// \brief Частота перерисовки, мс. Примерно 60 кадров в секунду.
constexpr int kRepaintIntervalMs = 16;

/// \brief Отступ содержимого от левого края, px.
constexpr int kLeftMargin = 6;

/// \brief Зазор между колонками слева и содержимым, в знакоместах.
constexpr int kGutterGap = 1;

/// \brief Ширина отметки направления, в знакоместах: «> », «< », «* ».
constexpr int kDirectionWidth = 2;

/// \brief Насколько приглушается цвет правила подсветки под текстом.
constexpr int kHighlightAlpha = 64;

/// \brief Отметка направления обмена.
QString directionMark(DataDirection direction)
{
    switch (direction) {
    case DataDirection::Tx:
        return QStringLiteral("< ");
    case DataDirection::System:
        return QStringLiteral("* ");
    case DataDirection::Rx:
        break;
    }
    return QStringLiteral("> ");
}

} // namespace

TerminalView::TerminalView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setMouseTracking(true);

    setTerminalFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // Перерисовка по таймеру, а не на каждую порцию данных: поток на 3 Мбод иначе занял
    // бы поток интерфейса целиком.
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setSingleShot(true);
    m_repaintTimer->setInterval(kRepaintIntervalMs);
    connect(m_repaintTimer, &QTimer::timeout, this, [this] {
        updateScrollBars();
        viewport()->update();
    });

    horizontalScrollBar()->setSingleStep(m_charWidth * 4);
}

TerminalView::~TerminalView() = default;

void TerminalView::setBuffer(TerminalBuffer *buffer)
{
    if (m_buffer == buffer)
        return;

    if (m_buffer)
        m_buffer->disconnect(this);

    m_buffer = buffer;

    if (m_buffer) {
        connect(m_buffer, &TerminalBuffer::linesAppended, this, &TerminalView::onLinesAppended);
        connect(m_buffer, &TerminalBuffer::lineUpdated, this, &TerminalView::onLineUpdated);
        connect(m_buffer, &TerminalBuffer::trimmed, this, &TerminalView::onTrimmed);
        connect(m_buffer, &TerminalBuffer::cleared, this, &TerminalView::onCleared);
    }

    clearSelection();
    rebuildVisible();
    scrollToBottom();
}

void TerminalView::setThemeManager(ThemeManager *theme)
{
    if (m_theme)
        m_theme->disconnect(this);

    m_theme = theme;
    if (!m_theme)
        return;

    m_palette.setThemeColors(m_theme->colors());
    connect(m_theme, &ThemeManager::themeChanged, this, [this] {
        // Палитра пересобирается, а не вычисляется один раз при разборе: иначе уже
        // накопленный вывод остался бы в цветах прежней темы.
        m_palette.setThemeColors(m_theme->colors());
        viewport()->update();
    });
}

void TerminalView::setTerminalFont(const QFont &font)
{
    m_font = font;
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
    updateMetrics();
    rebuildVisible();
    viewport()->update();
}

void TerminalView::updateMetrics()
{
    const QFontMetrics metrics(m_font);
    // Ширина знакоместа берётся у цифры: шрифт может оказаться не вполне моноширинным, а
    // цифры в любом шрифте одинаковой ширины почти всегда.
    m_charWidth = qMax(1, metrics.horizontalAdvance(u'0'));
    m_lineHeight = qMax(1, metrics.height());
    m_ascent = metrics.ascent();

    verticalScrollBar()->setSingleStep(1);
    verticalScrollBar()->setPageStep(qMax(1, viewport()->height() / m_lineHeight));
    horizontalScrollBar()->setSingleStep(m_charWidth * 4);
}

void TerminalView::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode)
        return;

    m_viewMode = mode;
    clearSelection();
    // Число рядов у каждой строки в режимах разное, поэтому список строится заново.
    rebuildVisible();
    scrollToBottom();
}

void TerminalView::setShowTimestamps(bool show)
{
    if (m_showTimestamps == show)
        return;
    m_showTimestamps = show;
    viewport()->update();
}

void TerminalView::setRelativeTimestamps(bool relative)
{
    if (m_relativeTimestamps == relative)
        return;
    m_relativeTimestamps = relative;
    viewport()->update();
}

void TerminalView::setTimestampFormat(const QString &format)
{
    m_timestampFormat = format;
    viewport()->update();
}

void TerminalView::setShowDirection(bool show)
{
    if (m_showDirection == show)
        return;
    m_showDirection = show;
    viewport()->update();
}

void TerminalView::setHexBytesPerRow(int bytes)
{
    const int clamped = qBound(1, bytes, 64);
    if (m_hexBytesPerRow == clamped)
        return;

    m_hexBytesPerRow = clamped;
    if (m_viewMode == ViewMode::Hex) {
        rebuildVisible();
        viewport()->update();
    }
}

void TerminalView::setHighlightRules(const HighlightRules &rules)
{
    m_highlightRules = rules;
    viewport()->update();
}

void TerminalView::setAnsiPalette(const QStringList &colors)
{
    m_palette.setCustomColors(colors);
    viewport()->update();
}

void TerminalView::setEncoding(QStringConverter::Encoding encoding)
{
    if (m_buffer)
        m_buffer->setEncoding(encoding);
}

void TerminalView::setSearchPattern(const QString &pattern, bool regularExpression,
                                    bool caseSensitive, bool wholeWords)
{
    m_searchActive = !pattern.isEmpty();

    if (m_searchActive) {
        // Обычный текст экранируется: пользователь, ищущий «rssi=-41», не должен получать
        // сюрпризов от того, что дефис и знак равенства что-то значат в регулярных
        // выражениях.
        QString source = regularExpression ? pattern : QRegularExpression::escape(pattern);
        if (wholeWords)
            source = QStringLiteral("\\b(?:%1)\\b").arg(source);

        m_searchRegex.setPattern(source);
        m_searchRegex.setPatternOptions(caseSensitive
                                            ? QRegularExpression::NoPatternOption
                                            : QRegularExpression::CaseInsensitiveOption);
        if (!m_searchRegex.isValid())
            m_searchActive = false;
        else
            m_searchRegex.optimize();
    }

    m_currentMatch = -1;

    // Фильтр показывает только совпавшие строки, поэтому смена образца меняет сам набор
    // видимых строк, а не только подсветку.
    if (m_filterEnabled)
        rebuildVisible();

    viewport()->update();
    Q_EMIT matchCountChanged(matchCount());
}

void TerminalView::setFilterEnabled(bool enabled)
{
    if (m_filterEnabled == enabled)
        return;

    m_filterEnabled = enabled;
    clearSelection();
    rebuildVisible();
    scrollToBottom();
}

QList<QPair<int, int>> TerminalView::searchMatches(const QString &text) const
{
    QList<QPair<int, int>> result;
    if (!m_searchActive || text.isEmpty())
        return result;

    QRegularExpressionMatchIterator it = m_searchRegex.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (match.capturedLength() > 0)
            result.append({match.capturedStart(), match.capturedLength()});
    }
    return result;
}

bool TerminalView::passesFilter(const TerminalBuffer::Line &line) const
{
    if (!m_filterEnabled || !m_searchActive)
        return true;
    // Системные сообщения не скрываются: «порт открыт», «устройство отключено» и ошибки
    // нужны всегда, иначе фильтр прячет причину происходящего.
    if (line.direction == DataDirection::System)
        return true;
    return m_searchRegex.match(line.text).hasMatch();
}

int TerminalView::rowsForLine(const TerminalBuffer::Line &line) const
{
    if (m_viewMode == ViewMode::Text)
        return 1;

    if (line.raw.isEmpty()) {
        // Системные сообщения байтов не имеют, но показать их в HEX-режиме всё равно
        // нужно: без них пропали бы отметки об открытии порта и об ошибках.
        return 1;
    }
    return int((line.raw.size() + m_hexBytesPerRow - 1) / m_hexBytesPerRow);
}

void TerminalView::rebuildVisible()
{
    m_visible.clear();
    m_totalRows = 0;

    if (!m_buffer) {
        updateScrollBars();
        return;
    }

    const qint64 end = m_buffer->nextLineNumber();
    for (qint64 number = m_buffer->firstLineNumber(); number < end; ++number)
        appendVisible(number);

    updateScrollBars();
}

void TerminalView::appendVisible(qint64 lineNumber)
{
    const TerminalBuffer::Line *line = m_buffer->line(lineNumber);
    if (!line || !passesFilter(*line))
        return;

    const int rows = rowsForLine(*line);
    m_visible.push_back(VisibleLine{lineNumber, rows});
    m_totalRows += rows;
}

int TerminalView::visibleIndexOf(qint64 lineNumber) const
{
    // Список отсортирован по номеру строки, поэтому двоичный поиск. Линейный обход
    // выполнялся бы на каждое обновление строки — при частом выводе это заметно.
    const auto it = std::lower_bound(m_visible.begin(), m_visible.end(), lineNumber,
                                     [](const VisibleLine &entry, qint64 value) {
                                         return entry.lineNumber < value;
                                     });
    if (it == m_visible.end() || it->lineNumber != lineNumber)
        return -1;
    return int(it - m_visible.begin());
}

int TerminalView::cachedRowsFor(qint64 lineNumber) const
{
    const int index = visibleIndexOf(lineNumber);
    return index >= 0 ? m_visible[size_t(index)].rows : 1;
}

qint64 TerminalView::rowOfVisibleIndex(int index) const
{
    if (m_viewMode == ViewMode::Text)
        return index;

    qint64 rows = 0;
    for (int i = 0; i < index && i < int(m_visible.size()); ++i)
        rows += m_visible[size_t(i)].rows;
    return rows;
}

TerminalView::Position TerminalView::positionAtRow(qint64 rowIndex) const
{
    Position position;
    if (m_visible.empty())
        return position;

    qint64 remaining = qBound(qint64(0), rowIndex, qMax(qint64(0), m_totalRows - 1));

    // В текстовом режиме ряд и строка — одно и то же, поэтому обход не нужен. Это самый
    // частый случай, и он вызывается на каждой перерисовке.
    if (m_viewMode == ViewMode::Text) {
        const size_t index = size_t(qMin(remaining, qint64(m_visible.size()) - 1));
        position.lineNumber = m_visible[index].lineNumber;
        position.row = 0;
        return position;
    }

    for (size_t i = 0; i < m_visible.size(); ++i) {
        if (remaining < m_visible[i].rows) {
            position.lineNumber = m_visible[i].lineNumber;
            position.row = int(remaining);
            return position;
        }
        remaining -= m_visible[i].rows;
    }

    position.lineNumber = m_visible.back().lineNumber;
    position.row = qMax(0, m_visible.back().rows - 1);
    return position;
}

QString TerminalView::rowText(const TerminalBuffer::Line &line, int row) const
{
    if (m_viewMode == ViewMode::Text)
        return line.text;

    if (line.raw.isEmpty())
        return line.text;

    const qsizetype offset = qsizetype(row) * m_hexBytesPerRow;
    if (offset >= line.raw.size())
        return {};

    const QByteArray slice = line.raw.mid(offset, m_hexBytesPerRow);

    // Классический дамп: смещение, байты, колонка печатных символов. Байты дополняются
    // пробелами до полной ширины, чтобы колонка ASCII не съезжала на последнем ряду.
    QString result = QStringLiteral("%1  ").arg(offset, 6, 16, QLatin1Char('0')).toUpper();

    QString hexPart;
    hexPart.reserve(m_hexBytesPerRow * 3);
    for (int i = 0; i < m_hexBytesPerRow; ++i) {
        if (i < slice.size()) {
            hexPart += QStringLiteral("%1 ")
                           .arg(quint8(slice.at(i)), 2, 16, QLatin1Char('0'))
                           .toUpper();
        } else {
            hexPart += QStringLiteral("   ");
        }
        // Разделитель посередине помогает считать байты глазами.
        if (m_hexBytesPerRow % 2 == 0 && i == m_hexBytesPerRow / 2 - 1)
            hexPart += u' ';
    }

    QString asciiPart;
    asciiPart.reserve(slice.size());
    for (const char byte : slice) {
        const auto value = quint8(byte);
        asciiPart += (value >= 0x20 && value < 0x7F) ? QChar(value) : QChar(u'.');
    }

    return result + hexPart + u' ' + asciiPart;
}

QString TerminalView::timestampText(const TerminalBuffer::Line &line, qint64 lineNumber) const
{
    if (!m_showTimestamps || !line.wallClock.isValid())
        return {};

    if (!m_relativeTimestamps)
        return line.wallClock.toString(m_timestampFormat);

    // Разница с предыдущей строкой: именно она отвечает на вопрос «сколько устройство
    // думало», ради которого метки чаще всего и включают.
    const TerminalBuffer::Line *previous = m_buffer->line(lineNumber - 1);
    if (!previous || !previous->wallClock.isValid())
        return QStringLiteral("   +0.000");

    const qint64 deltaMs = previous->wallClock.msecsTo(line.wallClock);
    return QStringLiteral("%1%2.%3")
        .arg(deltaMs < 0 ? u'-' : u'+')
        .arg(qAbs(deltaMs) / 1000, 4)
        .arg(qAbs(deltaMs) % 1000, 3, 10, QLatin1Char('0'));
}

int TerminalView::timestampColumns() const
{
    // Плюс знакоместо на пробел: без него метка времени и отметка направления слипаются
    // в «11:48:32.313>» и читаются как одно слово.
    return (m_relativeTimestamps ? 9 : int(m_timestampFormat.size())) + 1;
}

int TerminalView::gutterWidth() const
{
    int columns = 0;
    if (m_showTimestamps)
        columns += timestampColumns();
    if (m_showDirection)
        columns += kDirectionWidth;
    if (columns > 0)
        columns += kGutterGap;
    return columns * m_charWidth;
}

void TerminalView::updateScrollBars()
{
    const int visibleRows = qMax(1, viewport()->height() / m_lineHeight);

    verticalScrollBar()->setRange(0, int(qMax(qint64(0), m_totalRows - visibleRows)));
    verticalScrollBar()->setPageStep(visibleRows);

    // Ширину содержимого меряем не по всему буферу, а по видимому окну: обход сотен тысяч
    // строк на каждой перерисовке свёл бы на нет весь выигрыш от отрисовки только
    // видимого.
    int widestColumns = 0;
    if (m_buffer && !m_visible.empty()) {
        const Position top = positionAtRow(verticalScrollBar()->value());
        int index = qMax(0, visibleIndexOf(top.lineNumber));
        int rowsShown = 0;
        while (index < int(m_visible.size()) && rowsShown <= visibleRows) {
            const TerminalBuffer::Line *line = m_buffer->line(m_visible[size_t(index)].lineNumber);
            if (!line)
                break;
            for (int row = 0; row < m_visible[size_t(index)].rows && rowsShown <= visibleRows;
                 ++row, ++rowsShown) {
                widestColumns = qMax(widestColumns, int(rowText(*line, row).size()));
            }
            ++index;
        }
    }

    const int contentWidth = kLeftMargin + gutterWidth() + widestColumns * m_charWidth;
    horizontalScrollBar()->setRange(0, qMax(0, contentWidth - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
}

int TerminalView::matchCount() const
{
    if (!m_searchActive || !m_buffer)
        return 0;

    // В режиме фильтра список видимых строк и есть набор совпавших — считать заново
    // незачем.
    if (m_filterEnabled)
        return int(m_visible.size());

    int count = 0;
    for (const VisibleLine &entry : m_visible) {
        const TerminalBuffer::Line *line = m_buffer->line(entry.lineNumber);
        if (line && m_searchRegex.match(line->text).hasMatch())
            ++count;
    }
    return count;
}

void TerminalView::scheduleRepaint()
{
    if (!m_repaintTimer->isActive())
        m_repaintTimer->start();
}

void TerminalView::onLinesAppended(qint64 firstLineNumber, qint64 count)
{
    Q_UNUSED(count);
    if (!m_buffer)
        return;

    // Последняя запись могла относиться к строке, которая с тех пор доросла: сигнал
    // приходит и на изменение открытой строки.
    const qint64 known = m_visible.empty() ? firstLineNumber
                                           : m_visible.back().lineNumber + 1;

    for (qint64 number = qMax(firstLineNumber, known); number < m_buffer->nextLineNumber();
         ++number) {
        appendVisible(number);
    }

    // Строка, начатая ранее, могла дорасти до нового числа рядов.
    if (!m_visible.empty()) {
        VisibleLine &last = m_visible.back();
        if (const TerminalBuffer::Line *line = m_buffer->line(last.lineNumber)) {
            const int rows = rowsForLine(*line);
            m_totalRows += rows - last.rows;
            last.rows = rows;
        }
    }

    if (m_followTail) {
        const int visibleRows = qMax(1, viewport()->height() / m_lineHeight);
        verticalScrollBar()->setRange(0, int(qMax(qint64(0), m_totalRows - visibleRows)));
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }

    scheduleRepaint();
}

void TerminalView::onLineUpdated(qint64 lineNumber)
{
    if (!m_buffer)
        return;

    const TerminalBuffer::Line *line = m_buffer->line(lineNumber);
    if (!line)
        return;

    const int index = visibleIndexOf(lineNumber);

    if (index < 0) {
        // Строка была отфильтрована, но дописанный текст мог сделать её подходящей.
        if (passesFilter(*line) && (m_visible.empty()
                                    || m_visible.back().lineNumber < lineNumber)) {
            appendVisible(lineNumber);
        }
    } else {
        const int rows = rowsForLine(*line);
        m_totalRows += rows - m_visible[size_t(index)].rows;
        m_visible[size_t(index)].rows = rows;
    }

    if (m_followTail)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());

    scheduleRepaint();
}

void TerminalView::onTrimmed(qint64 newFirstLineNumber)
{
    qint64 removedRows = 0;
    while (!m_visible.empty() && m_visible.front().lineNumber < newFirstLineNumber) {
        removedRows += m_visible.front().rows;
        m_visible.pop_front();
    }
    m_totalRows -= removedRows;

    // Вид остаётся на прежнем содержимом: без сдвига позиции читаемое место уехало бы
    // вверх ровно на столько рядов, сколько вытеснено.
    if (!m_followTail && removedRows > 0) {
        const int value = verticalScrollBar()->value() - int(removedRows);
        verticalScrollBar()->setValue(qMax(0, value));
    }

    scheduleRepaint();
}

void TerminalView::onCleared()
{
    m_visible.clear();
    m_totalRows = 0;
    m_currentMatch = -1;
    clearSelection();
    updateScrollBars();
    viewport()->update();
}

void TerminalView::setPlaceholderText(const QString &text)
{
    if (m_placeholder == text)
        return;
    m_placeholder = text;
    if (!m_buffer || m_visible.empty())
        viewport()->update();
}

void TerminalView::drawPlaceholder(QPainter &painter, const ThemeColors &colors) const
{
    // Фильтр, скрывший всё до последней строки, объясняется точнее общей подсказки: данные
    // есть, их просто не видно, и снять это одним щелчком там же, откуда включили.
    const bool hiddenByFilter =
        m_filterEnabled && m_buffer && m_buffer->lineCount() > 0;
    const QString text = hiddenByFilter ? tr("No lines match the filter") : m_placeholder;
    if (text.isEmpty())
        return;

    // Шрифт интерфейса, а не терминала: это подпись программы, а не пришедшие данные, и
    // моноширинный набор выдал бы её за строку вывода.
    painter.setFont(font());
    painter.setPen(colors.textMuted);
    painter.drawText(viewport()->rect(), Qt::AlignCenter, text);
}

void TerminalView::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    painter.setFont(m_font);

    const ThemeColors colors = m_theme ? m_theme->colors() : ThemeColors{};
    painter.fillRect(event->rect(), colors.base);

    if (!m_buffer || m_visible.empty()) {
        drawPlaceholder(painter, colors);
        return;
    }

    const int visibleRows = viewport()->height() / m_lineHeight + 2;
    const int gutter = gutterWidth();
    const int xOffset = -horizontalScrollBar()->value();

    Position from;
    Position to;
    const bool hasSelection = selectionRange(&from, &to);

    const Position top = positionAtRow(verticalScrollBar()->value());
    int index = qMax(0, visibleIndexOf(top.lineNumber));
    int row = top.row;
    int y = 0;

    for (int painted = 0; painted < visibleRows && index < int(m_visible.size()); ++painted) {
        const qint64 lineNumber = m_visible[size_t(index)].lineNumber;
        const TerminalBuffer::Line *line = m_buffer->line(lineNumber);
        if (!line)
            break;

        const int textY = y + m_ascent;
        const QString content = rowText(*line, row);

        // Правило подсветки красит всю строку целиком: приглушённая заливка позволяет
        // выхватить нужные строки боковым зрением, не мешая читать текст.
        const int ruleIndex = m_highlightRules.isEmpty() ? -1
                                                         : m_highlightRules.match(line->text);
        if (ruleIndex >= 0) {
            const quint32 rgb = m_highlightRules.colorAt(ruleIndex);
            QColor tint(int((rgb >> 16) & 0xFF), int((rgb >> 8) & 0xFF), int(rgb & 0xFF));
            tint.setAlpha(kHighlightAlpha);
            painter.fillRect(0, y, viewport()->width(), m_lineHeight, tint);
        }

        // Колонки слева не прокручиваются вбок: метка времени и направление должны
        // оставаться на виду при горизонтальной прокрутке длинной строки.
        if (row == 0) {
            int gutterX = kLeftMargin;
            painter.setPen(colors.textMuted);

            if (m_showTimestamps) {
                painter.drawText(gutterX, textY, timestampText(*line, lineNumber));
                gutterX += timestampColumns() * m_charWidth;
            }
            if (m_showDirection) {
                switch (line->direction) {
                case DataDirection::Tx:
                    painter.setPen(colors.txText);
                    break;
                case DataDirection::System:
                    painter.setPen(colors.accent);
                    break;
                case DataDirection::Rx:
                    painter.setPen(colors.textMuted);
                    break;
                }
                painter.drawText(gutterX, textY, directionMark(line->direction));
            }
        }

        const int contentX = kLeftMargin + gutter + xOffset;

        // Совпадения поиска — под текстом, но над подсветкой правил.
        if (m_searchActive) {
            QColor matchColor = colors.accent;
            matchColor.setAlpha(90);
            const QList<QPair<int, int>> matches = searchMatches(content);
            for (const auto &[start, length] : matches) {
                painter.fillRect(contentX + start * m_charWidth, y, length * m_charWidth,
                                 m_lineHeight, matchColor);
            }
        }

        // Выделение рисуется целым прямоугольником на ряд: посимвольная заливка на
        // длинных строках заметно медленнее и выглядит так же.
        if (hasSelection) {
            const Position rowStart{lineNumber, row, 0};
            const Position rowEnd{lineNumber, row, int(content.size())};
            if (rowEnd >= from && rowStart <= to) {
                const int startColumn = (rowStart >= from) ? 0 : from.column;
                const int endColumn = (rowEnd <= to) ? int(content.size()) : to.column;
                if (endColumn > startColumn) {
                    painter.fillRect(contentX + startColumn * m_charWidth, y,
                                     (endColumn - startColumn) * m_charWidth, m_lineHeight,
                                     colors.selection);
                }
            }
        }

        if (m_viewMode == ViewMode::Hex || line->direction == DataDirection::System) {
            painter.setPen(line->direction == DataDirection::System ? colors.accent
                                                                    : colors.rxText);
            painter.drawText(contentX, textY, content);
        } else {
            // Текстовый режим: рисуем отрезками, у каждого своё оформление.
            const QColor defaultForeground =
                line->direction == DataDirection::Tx ? colors.txText : colors.rxText;

            for (const TerminalBuffer::StyleRun &styleRun : line->runs) {
                if (styleRun.start >= content.size())
                    break;
                const int length = qMin(styleRun.length, int(content.size()) - styleRun.start);
                if (length <= 0)
                    continue;

                const int runX = contentX + styleRun.start * m_charWidth;

                const QColor background = m_palette.background(styleRun.style, colors.base);
                if (background.isValid())
                    painter.fillRect(runX, y, length * m_charWidth, m_lineHeight, background);

                QFont runFont = m_font;
                if (styleRun.style.bold)
                    runFont.setBold(true);
                if (styleRun.style.italic)
                    runFont.setItalic(true);
                if (styleRun.style.underline)
                    runFont.setUnderline(true);
                if (styleRun.style.strikeOut)
                    runFont.setStrikeOut(true);
                painter.setFont(runFont);

                painter.setPen(m_palette.foreground(styleRun.style, defaultForeground));
                painter.drawText(runX, textY, content.mid(styleRun.start, length));
            }
            painter.setFont(m_font);
        }

        y += m_lineHeight;

        // Переход к следующему ряду: внутри строки либо в начало следующей.
        if (row + 1 < m_visible[size_t(index)].rows) {
            ++row;
        } else {
            ++index;
            row = 0;
        }
    }
}

void TerminalView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
    if (m_followTail)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalView::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);

    // Ключ умного автоскролла: слежение включено ровно тогда, когда просмотр внизу.
    // Прокрутка вверх его снимает, возврат вниз — включает, и никаких особых случаев.
    const bool atBottom = verticalScrollBar()->value() >= verticalScrollBar()->maximum();
    if (atBottom != m_followTail) {
        m_followTail = atBottom;
        Q_EMIT followTailChanged(m_followTail);
    }

    viewport()->update();
}

void TerminalView::setFollowTail(bool follow)
{
    if (m_followTail == follow)
        return;

    m_followTail = follow;
    if (follow)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    Q_EMIT followTailChanged(m_followTail);
}

void TerminalView::scrollToBottom()
{
    updateScrollBars();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    if (!m_followTail) {
        m_followTail = true;
        Q_EMIT followTailChanged(true);
    }
}

void TerminalView::findNext()
{
    stepMatch(+1);
}

void TerminalView::findPrevious()
{
    stepMatch(-1);
}

void TerminalView::stepMatch(int direction)
{
    if (!m_searchActive || !m_buffer || m_visible.empty())
        return;

    const int total = int(m_visible.size());
    int start = m_currentMatch;
    if (start < 0)
        start = direction > 0 ? -1 : total;

    for (int step = 1; step <= total; ++step) {
        const int index = ((start + direction * step) % total + total) % total;
        const qint64 lineNumber = m_visible[size_t(index)].lineNumber;
        const TerminalBuffer::Line *line = m_buffer->line(lineNumber);
        if (!line)
            continue;

        const QList<QPair<int, int>> matches = searchMatches(line->text);
        if (matches.isEmpty())
            continue;

        m_currentMatch = index;

        // Совпадение выделяется целиком: следующее действие пользователя почти всегда —
        // скопировать найденное.
        const auto &[matchStart, matchLength] = matches.first();
        m_selectionAnchor = Position{lineNumber, 0, matchStart};
        m_selectionCursor = Position{lineNumber, 0, matchStart + matchLength};

        // Строка ставится в середину области просмотра, а не к краю: контекст вокруг
        // найденного обычно и есть то, ради чего искали.
        const qint64 rowIndex = rowOfVisibleIndex(index);
        const int half = viewport()->height() / m_lineHeight / 2;
        setFollowTail(false);
        verticalScrollBar()->setValue(int(qMax(qint64(0), rowIndex - half)));

        viewport()->update();
        return;
    }
}

TerminalView::Position TerminalView::positionAt(const QPoint &point) const
{
    Position position;
    if (!m_buffer || m_visible.empty())
        return position;

    const int rowOffset = qMax(0, point.y()) / m_lineHeight;
    position = positionAtRow(verticalScrollBar()->value() + rowOffset);
    if (!position.isValid())
        return position;

    const TerminalBuffer::Line *line = m_buffer->line(position.lineNumber);
    if (!line)
        return position;

    const int contentX = kLeftMargin + gutterWidth() - horizontalScrollBar()->value();
    // Округление к ближайшей границе знакоместа: так курсор ставится там, где его ждёт
    // человек, а не всегда левее.
    const int column = qRound(double(point.x() - contentX) / m_charWidth);
    position.column = qBound(0, column, int(rowText(*line, position.row).size()));
    return position;
}

bool TerminalView::selectionRange(Position *from, Position *to) const
{
    if (!m_selectionAnchor.isValid() || !m_selectionCursor.isValid())
        return false;
    if (m_selectionAnchor == m_selectionCursor)
        return false;

    if (m_selectionAnchor < m_selectionCursor) {
        *from = m_selectionAnchor;
        *to = m_selectionCursor;
    } else {
        *from = m_selectionCursor;
        *to = m_selectionAnchor;
    }
    return true;
}

bool TerminalView::hasSelection() const
{
    Position from;
    Position to;
    return selectionRange(&from, &to);
}

void TerminalView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    m_selectionAnchor = positionAt(event->pos());
    m_selectionCursor = m_selectionAnchor;
    m_selecting = true;
    viewport()->update();
}

void TerminalView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_selecting) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    m_selectionCursor = positionAt(event->pos());

    // Протаскивание за край области прокручивает содержимое — иначе выделить больше
    // экрана было бы невозможно.
    if (event->pos().y() < 0)
        verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
    else if (event->pos().y() > viewport()->height())
        verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);

    viewport()->update();
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_selecting = false;
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_buffer) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }

    // Двойной щелчок выделяет слово: разделителями считаем всё, что не буква, не цифра и
    // не подчёркивание.
    const Position at = positionAt(event->pos());
    const TerminalBuffer::Line *line = at.isValid() ? m_buffer->line(at.lineNumber) : nullptr;
    if (!line)
        return;

    const QString text = rowText(*line, at.row);
    if (text.isEmpty())
        return;

    const auto isWordChar = [](QChar ch) { return ch.isLetterOrNumber() || ch == u'_'; };

    int start = qBound(0, at.column, int(text.size()) - 1);
    int end = start;
    while (start > 0 && isWordChar(text.at(start - 1)))
        --start;
    while (end < text.size() && isWordChar(text.at(end)))
        ++end;

    m_selectionAnchor = Position{at.lineNumber, at.row, start};
    m_selectionCursor = Position{at.lineNumber, at.row, end};
    viewport()->update();
}

void TerminalView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::FindNext)) {
        findNext();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::FindPrevious)) {
        findPrevious();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_End && event->modifiers().testFlag(Qt::ControlModifier)) {
        scrollToBottom();
        event->accept();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void TerminalView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction *copy = menu.addAction(tr("Copy"), this, &TerminalView::copySelection);
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(hasSelection());

    menu.addAction(tr("Select All"), this, &TerminalView::selectAll)
        ->setShortcut(QKeySequence::SelectAll);

    menu.addSeparator();
    menu.addAction(tr("Scroll to Bottom"), this, &TerminalView::scrollToBottom);

    menu.exec(event->globalPos());
}

bool TerminalView::viewportEvent(QEvent *event)
{
    // Всплывающая подсказка показывает полную метку времени строки под курсором: в
    // относительном режиме абсолютное время иначе было бы недоступно вовсе.
    if (event->type() == QEvent::ToolTip && m_buffer && m_showTimestamps) {
        auto *helpEvent = static_cast<QHelpEvent *>(event);
        const Position at = positionAt(helpEvent->pos());
        if (const TerminalBuffer::Line *line = at.isValid() ? m_buffer->line(at.lineNumber)
                                                            : nullptr) {
            if (line->wallClock.isValid()) {
                QToolTip::showText(helpEvent->globalPos(),
                                   line->wallClock.toString(Qt::ISODateWithMs), viewport());
                return true;
            }
        }
        QToolTip::hideText();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void TerminalView::clearSelection()
{
    m_selectionAnchor = {};
    m_selectionCursor = {};
    m_selecting = false;
    viewport()->update();
}

void TerminalView::selectAll()
{
    if (!m_buffer || m_visible.empty())
        return;

    const VisibleLine &last = m_visible.back();
    const TerminalBuffer::Line *line = m_buffer->line(last.lineNumber);

    m_selectionAnchor = Position{m_visible.front().lineNumber, 0, 0};
    m_selectionCursor = Position{last.lineNumber, qMax(0, last.rows - 1),
                                 line ? int(rowText(*line, last.rows - 1).size()) : 0};
    viewport()->update();
}

QString TerminalView::selectedText() const
{
    Position from;
    Position to;
    if (!m_buffer || !selectionRange(&from, &to))
        return {};

    QStringList rows;

    int index = visibleIndexOf(from.lineNumber);
    if (index < 0)
        return {};
    int row = from.row;

    while (index < int(m_visible.size())) {
        const qint64 lineNumber = m_visible[size_t(index)].lineNumber;
        const Position position{lineNumber, row, 0};
        if (position > to)
            break;

        const TerminalBuffer::Line *line = m_buffer->line(lineNumber);
        if (!line)
            break;

        const QString content = rowText(*line, row);
        const int start = (lineNumber == from.lineNumber && row == from.row) ? from.column : 0;
        const int end = (lineNumber == to.lineNumber && row == to.row) ? to.column
                                                                      : int(content.size());

        rows.append(content.mid(start, qMax(0, end - start)));

        if (lineNumber == to.lineNumber && row == to.row)
            break;

        if (row + 1 < m_visible[size_t(index)].rows) {
            ++row;
        } else {
            ++index;
            row = 0;
        }
    }

    return rows.join(u'\n');
}

void TerminalView::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

} // namespace spotty
