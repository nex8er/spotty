/**
 * \file TerminalView.h
 * \brief Виджет вывода терминала с собственной отрисовкой.
 */
#pragma once

#include "theme/AnsiPalette.h"

#include "ScrollMarkerBar.h"

#include <spotty/data/CsvDetector.h>
#include <spotty/data/HighlightRules.h>
#include <terminal/TerminalBuffer.h>

#include <QAbstractScrollArea>
#include <QFont>
#include <QRegularExpression>
#include <QString>

#include <deque>

class QTimer;

namespace spotty {

class ThemeManager;

/**
 * \class TerminalView
 * \brief Отображение буфера терминала: текст с оформлением ANSI либо HEX-дамп.
 *
 * \par Почему не QPlainTextEdit
 *
 * Требуются колонка меток времени, отметка направления, режим HEX, правила подсветки,
 * режим фильтра и буфер в сотни тысяч строк. Каждому из этих пунктов QPlainTextEdit
 * сопротивляется, а посимвольный QTextCharFormat на большом буфере ещё и медленный.
 * Плата за собственную отрисовку — выделение и копирование, написанные вручную.
 *
 * Рисуются только видимые строки, поэтому размер буфера на скорость отрисовки не влияет.
 *
 * \par Умный автоскролл
 *
 * Пока просмотр внизу, вид следует за выводом. Прокрутка вверх снимает слежение, и новые
 * данные больше не уводят от читаемого места. Возврат в самый низ слежение включает
 * обратно. Признак хранится в #m_followTail.
 *
 * \par Обновление
 *
 * Перерисовка не идёт на каждую порцию данных: сигналы буфера лишь взводят таймер, и
 * перерисовка происходит с частотой около 60 Гц. Без этого поток на 3 Мбод занял бы весь
 * поток интерфейса перерисовками.
 *
 * \par Строки и ряды
 *
 * Строка буфера в режиме HEX занимает несколько рядов на экране. Прокрутка ведётся в
 * рядах, а не в строках: иначе пакет в четыре килобайта, занимающий 256 рядов,
 * прокручивался бы одним прыжком и его нельзя было бы досмотреть. Число рядов каждой
 * строки поддерживается в #m_rowCounts, чтобы не пересчитывать весь буфер при каждой
 * прокрутке.
 */
class TerminalView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    /// \brief Способ показа данных.
    enum class ViewMode {
        Text, ///< Текст с оформлением ANSI.
        Hex,  ///< Шестнадцатеричный дамп со смещениями и колонкой ASCII.
    };
    Q_ENUM(ViewMode)

    explicit TerminalView(QWidget *parent = nullptr);
    ~TerminalView() override;

    /// \brief Показываемый буфер. Виджет им не владеет.
    void setBuffer(TerminalBuffer *buffer);
    TerminalBuffer *buffer() const { return m_buffer; }

    /// \brief Источник цветов. Виджет подписывается на смену темы.
    void setThemeManager(ThemeManager *theme);

    ViewMode viewMode() const { return m_viewMode; }
    void setViewMode(ViewMode mode);

    /// \brief Показывать колонку меток времени.
    void setShowTimestamps(bool show);
    bool showTimestamps() const { return m_showTimestamps; }

    /**
     * \brief Показывать номера строк в колонке слева.
     *
     * Номера сквозные и совпадают с нумерацией буфера, а не с положением на экране: буфер
     * подрезается спереди, и «первая видимая строка» — это не первая строка сеанса. Так
     * номер отвечает на вопрос «сколько всего пришло», а не «сколько влезло в окно».
     */
    void setShowLineNumbers(bool show);
    bool showLineNumbers() const { return m_showLineNumbers; }

    /**
     * \brief Скрывать из вывода строки, состоящие из чисел через разделитель.
     *
     * Это фильтр **показа**, а не потока: строки по-прежнему попадают в буфер, их видят
     * поиск, график и журнал. Вырезать их из потока значило бы лишить данных того, ради
     * кого их и присылают.
     */
    void setCsvFilterEnabled(bool enabled);
    bool csvFilterEnabled() const { return m_csvFilterEnabled; }

    /// \brief Разделитель полей для распознавания данных.
    void setCsvSeparator(QChar separator);

    /**
     * \brief Показывать метку транспорта в колонке слева.
     *
     * Включается, когда открыт второй интерфейс: строки идут в общий буфер вперемешку по
     * времени, и без метки понять, кто что сказал, невозможно. При одном транспорте
     * колонка не нужна и только отнимала бы ширину.
     */
    void setShowSource(bool show);

    /**
     * \brief Показывать метку времени относительно предыдущей строки, а не абсолютную.
     *
     * Разница между строками отвечает на вопрос «сколько устройство думало», ради
     * которого метки чаще всего и включают.
     */
    void setRelativeTimestamps(bool relative);
    bool relativeTimestamps() const { return m_relativeTimestamps; }

    /// \brief Формат абсолютной метки времени в записи QDateTime::toString().
    void setTimestampFormat(const QString &format);

    /// \brief Показывать отметку направления обмена.
    void setShowDirection(bool show);
    bool showDirection() const { return m_showDirection; }

    /**
     * \brief Сделать указанную строку нулём пользовательской нумерации.
     * \param lineNumber Сквозной номер строки в буфере.
     *
     * Строки до неё получают отрицательные номера, поэтому ориентир можно поставить
     * посреди уже накопленного журнала, не теряя контекст до него.
     */
    void setLineNumberOrigin(qint64 lineNumber);

    /// \brief Байтов в ряду HEX-дампа.
    void setHexBytesPerRow(int bytes);
    int hexBytesPerRow() const { return m_hexBytesPerRow; }

    /// \brief Шрифт вывода. Ожидается моноширинный.
    void setTerminalFont(const QFont &font);

    /// \return `true`, если вид следует за выводом.
    bool followTail() const { return m_followTail; }
    void setFollowTail(bool follow);

    /// \brief Текст выделения; пустая строка, если ничего не выделено.
    QString selectedText() const;
    bool hasSelection() const;

    /// \brief Правила подсветки строк. Виджет хранит копию.
    void setHighlightRules(const HighlightRules &rules);

    /**
     * \brief Свои шестнадцать базовых цветов ANSI вместо цветов темы.
     * \param colors Ровно шестнадцать значений `"#rrggbb"`; пустой список возвращает
     *        цвета темы.
     */
    void setAnsiPalette(const QStringList &colors);

    /// \brief Действующие шестнадцать базовых цветов ANSI.
    QStringList ansiPalette() const { return m_palette.baseColors(); }

    /// \brief Кодировка принимаемого текста. Передаётся буферу.
    void setEncoding(QStringConverter::Encoding encoding);

    /**
     * \brief Задать образец поиска.
     * \param pattern Текст или регулярное выражение; пустая строка снимает поиск.
     * \param regularExpression Трактовать \p pattern как регулярное выражение.
     * \param caseSensitive Учитывать регистр.
     * \param wholeWords Искать только слова целиком.
     *
     * Совпадения подсвечиваются во всех видимых строках.
     */
    void setSearchPattern(const QString &pattern, bool regularExpression, bool caseSensitive,
                          bool wholeWords);

    /**
     * \brief Показывать только строки, подходящие под образец поиска.
     *
     * Режим фильтра, а не поиска: несовпавшие строки скрываются, и в терминале остаётся
     * только то, что нужно. Прокрутка и выделение работают по отфильтрованному набору.
     */
    void setFilterEnabled(bool enabled);
    bool filterEnabled() const { return m_filterEnabled; }

    /// \brief Число строк, подходящих под образец поиска.
    int matchCount() const;

    /**
     * \brief Порядковый номер текущего совпадения, считая с единицы.
     * \return 0, если поиск не задан или переход ни разу не делали.
     */
    int currentMatch() const;

    /**
     * \brief Что показать вместо пустого поля вывода.
     * \param text Одна короткая фраза; пустая строка убирает подсказку.
     *
     * Пустой чёрный прямоугольник во всё окно не отвечает ни на один вопрос: ждать ли
     * данных, выбран ли интерфейс, не сломалось ли что-нибудь. Ответ знает не терминал, а
     * владелец сессии, поэтому текст приходит извне, а не выводится здесь из состояния.
     *
     * \note Собственная подсказка «фильтр ничего не оставил» этот текст перекрывает: она
     *       относится к тому же пустому месту, но объясняет его точнее.
     */
    void setPlaceholderText(const QString &text);

    /// \brief Перейти к следующему совпадению и выделить его.
    void findNext();

    /// \brief Перейти к предыдущему совпадению.
    void findPrevious();

public Q_SLOTS:
    void copySelection();
    void selectAll();
    void clearSelection();

    /// \brief Прокрутить в самый низ и включить слежение за выводом.
    void scrollToBottom();

Q_SIGNALS:
    /// \brief Изменилось слежение за выводом — панель может показать это кнопкой.
    void followTailChanged(bool following);

    /// \brief Изменился набор совпадений или положение в нём.
    void matchCountChanged(int currentMatch, int totalMatches);

    /**
     * \brief Пользователь изменил кегль колесом с Ctrl.
     * \param pointSize Новый кегль.
     *
     * Терминал уже применил его к себе; сигнал нужен только чтобы владелец сохранил
     * величину в настройках. Иначе подобранный размер терялся бы при перезапуске — а
     * подбирают его ровно один раз и надолго.
     */
    void terminalFontSizeChanged(int pointSize);

    /**
     * \brief Нумерация строк переключена из контекстного меню.
     *
     * Кнопка в панели управления и пункт меню правят одно и то же состояние, поэтому та
     * из них, что не была нажата, обязана узнать об изменении — иначе кнопка показывала
     * бы одно, а терминал делал другое.
     */
    void showLineNumbersChanged(bool show);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    bool viewportEvent(QEvent *event) override;

private:
    /**
     * \brief Нарисовать подсказку на месте пустого вывода.
     * \param painter Художник по области просмотра.
     * \param colors Цвета текущей темы.
     */
    void drawPlaceholder(QPainter &painter, const ThemeColors &colors) const;

    /**
     * \struct Position
     * \brief Положение внутри содержимого: строка, ряд в строке, столбец.
     */
    struct Position
    {
        qint64 lineNumber = -1;
        int row = 0;
        int column = 0;

        auto operator<=>(const Position &) const = default;
        bool isValid() const { return lineNumber >= 0; }
    };

    /**
     * \struct VisibleLine
     * \brief Строка, попавшая на экран, вместе с числом занимаемых рядов.
     *
     * Отдельный список, а не прямая адресация буфера: в режиме фильтра показывается лишь
     * часть строк, и прокрутка должна вести себя так, будто остальных нет.
     */
    struct VisibleLine
    {
        qint64 lineNumber = 0;
        int rows = 1;
    };

    /// \brief Пересчитать метрики шрифта и геометрию колонок.
    void updateMetrics();

    /// \brief Пересчитать диапазоны полос прокрутки.
    void updateScrollBars();

    /// \brief Полностью пересобрать список видимых строк.
    void rebuildVisible();

    /// \brief Добавить строку в конец списка видимых, если она проходит фильтр.
    void appendVisible(qint64 lineNumber);

    /// \brief Проходит ли строка текущий фильтр.
    bool passesFilter(const TerminalBuffer::Line &line) const;

    /// \brief Сколько экранных рядов занимает строка.
    int rowsForLine(const TerminalBuffer::Line &line) const;

    /// \brief Индекс строки в списке видимых или -1.
    int visibleIndexOf(qint64 lineNumber) const;

    /// \brief Число рядов строки из кэша.
    int cachedRowsFor(qint64 lineNumber) const;

    /// \brief Строка и ряд по сквозному номеру ряда.
    Position positionAtRow(qint64 rowIndex) const;

    /// \brief Номер первого ряда строки по её индексу в списке видимых.
    qint64 rowOfVisibleIndex(int index) const;

    /// \brief Перейти к совпадению: -1 назад, +1 вперёд.
    void stepMatch(int direction);

    /// \brief Отрезки строки, совпавшие с образцом поиска.
    QList<QPair<int, int>> searchMatches(const QString &text) const;

    /// \brief Текст одного экранного ряда без колонок слева.
    QString rowText(const TerminalBuffer::Line &line, int row) const;

    /// \brief Ширина колонки меток времени в знакоместах, вместе с отбивкой.
    int timestampColumns() const;

    /// \brief Ширина колонок слева от содержимого.
    int gutterWidth() const;

    /// \brief Ширина колонки номеров в знакоместах; 0, когда номера скрыты.
    int lineNumberColumns() const;

    /**
     * \brief Пересчитать метки в дорожке полосы прокрутки.
     *
     * Обходит видимые строки — то есть весь буфер за вычетом отфильтрованных, — и потому
     * зовётся не на каждую пришедшую строку, а по таймеру: на буфере в сотни тысяч строк
     * полный обход при каждом обновлении съел бы поток интерфейса.
     */
    void updateScrollMarkers();

    /**
     * \brief Поставить пересчёт меток в очередь.
     *
     * Звать при любой правке того, из чего метки складываются: пришли строки, сменился
     * образец поиска, поменялись правила подсветки. Пока вызов стоял в одном лишь
     * updateScrollBars(), метки поиска появлялись только на живом потоке — на
     * остановленном выводе, где ищут чаще всего, их не было вовсе.
     */
    void scheduleMarkerUpdate();

    /// \brief Текст колонки меток времени для строки.
    QString timestampText(const TerminalBuffer::Line &line, qint64 lineNumber) const;

    /// \brief Положение содержимого по точке в области просмотра.
    Position positionAt(const QPoint &viewportPoint) const;

    /// \brief Нормализованные границы выделения.
    bool selectionRange(Position *from, Position *to) const;

    /// \brief Запросить перерисовку не чаще, чем позволяет частота обновления.
    void scheduleRepaint();

    void onLinesAppended(qint64 firstLineNumber, qint64 count);
    void onLineUpdated(qint64 lineNumber);
    void onTrimmed(qint64 newFirstLineNumber);
    void onCleared();

    TerminalBuffer *m_buffer = nullptr;
    ThemeManager *m_theme = nullptr;
    AnsiPalette m_palette;

    ViewMode m_viewMode = ViewMode::Text;
    bool m_showTimestamps = false;
    bool m_showLineNumbers = false;
    bool m_csvFilterEnabled = false;
    bool m_showSource = false;
    CsvDetector m_csvDetector;

    /// \brief Своя полоса прокрутки: рисует метки найденного прямо в дорожке.
    ScrollMarkerBar *m_markerBar = nullptr;

    /// \brief Откладывает пересчёт меток; см. updateScrollMarkers().
    QTimer *m_markerTimer = nullptr;

    /**
     * \brief Наибольшая встреченная ширина содержимого, знакомест.
     *
     * Диапазон горизонтальной полосы только растёт: измеренный по видимому окну он менялся
     * бы при каждой прокрутке, полоса мигала бы, и текст прыгал бы по вертикали на её
     * высоту. Сбрасывается вместе с буфером.
     */
    int m_widestColumnsSeen = 0;

    /// \brief Непотраченные пиксели вертикальной прокрутки трекпада; см. wheelEvent().
    int m_wheelRemainder = 0;
    bool m_relativeTimestamps = false;
    QString m_timestampFormat = QStringLiteral("HH:mm:ss.zzz");
    bool m_showDirection = true;
    int m_hexBytesPerRow = 16;

    /// \brief Сквозной номер строки, которая отображается нулём.
    qint64 m_lineNumberOrigin = -1;

    QFont m_font;
    int m_charWidth = 8;
    int m_lineHeight = 16;
    int m_ascent = 12;

    /// \brief Показываемые строки в порядке вывода; в режиме фильтра — только совпавшие.
    std::deque<VisibleLine> m_visible;
    qint64 m_totalRows = 0;

    bool m_followTail = true;

    HighlightRules m_highlightRules;

    QRegularExpression m_searchRegex;
    bool m_searchActive = false;
    bool m_filterEnabled = false;

    /// \brief Подсказка на месте пустого вывода; см. setPlaceholderText().
    QString m_placeholder;

    /// \brief Индекс текущего совпадения в m_visible; -1, если переход ещё не делался.
    int m_currentMatch = -1;

    Position m_selectionAnchor;
    Position m_selectionCursor;
    bool m_selecting = false;

    QTimer *m_repaintTimer = nullptr;
};

} // namespace spotty
