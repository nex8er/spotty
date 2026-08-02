/**
 * \file SearchPanel.h
 * \brief Панель поиска и правил подсветки.
 */
#pragma once

#include <HighlightRules.h>

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QToolButton;

namespace spotty {

struct AppContext;

/**
 * \class SearchPanel
 * \brief Поиск по выводу и постоянные правила подсветки.
 *
 * \par Поиск и фильтр — разные вещи
 *
 * Поиск подсвечивает совпадения и позволяет по ним ходить, оставляя весь вывод на виду.
 * Фильтр скрывает всё, кроме совпавших строк. Первое нужно, когда важен контекст вокруг
 * найденного; второе — когда из тысячи строк нужны три. Поэтому это флажок, а не два
 * разных режима поиска.
 *
 * \par Правила подсветки
 *
 * В отличие от поиска, правила действуют постоянно и раскрашивают строки по мере их
 * появления: `ERROR` красным, `WARN` жёлтым. Они сохраняются в настройках и переживают
 * перезапуск.
 */
class SearchPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SearchPanel(const AppContext &context, QWidget *parent = nullptr);

    /// \brief Показать число найденных строк.
    void setMatchCount(int count);

    /// \brief Передать фокус полю поиска.
    void focusSearch();

    /**
     * \brief Текущий набор правил подсветки.
     *
     * Нужен потому, что правила читаются из настроек в конструкторе — то есть до того,
     * как владелец успевает подключиться к highlightRulesChanged(). Начальное состояние
     * забирается этим методом, а дальше приходит сигналами.
     */
    const HighlightRules &highlightRules() const { return m_currentRules; }

Q_SIGNALS:
    /// \brief Изменился образец поиска или его настройки.
    void searchChanged(const QString &pattern, bool regularExpression, bool caseSensitive,
                       bool wholeWords);

    /// \brief Переключён режим фильтра.
    void filterToggled(bool enabled);

    void findNextRequested();
    void findPreviousRequested();

    /// \brief Изменился набор правил подсветки.
    void highlightRulesChanged(const HighlightRules &rules);

private:
    /// \brief Собрать образец из полей и сообщить о нём.
    void emitSearch();

    /// \brief Прочитать правила из таблицы, сохранить и сообщить о них.
    void commitRules();

    /// \brief Заполнить таблицу из набора правил.
    void populateRules(const HighlightRules &rules);

    /// \brief Добавить строку таблицы под одно правило.
    void appendRuleRow(const HighlightRule &rule);

    const AppContext &m_context;

    QLineEdit *m_pattern = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QCheckBox *m_wholeWords = nullptr;
    QCheckBox *m_filter = nullptr;
    QToolButton *m_previous = nullptr;
    QToolButton *m_next = nullptr;
    QLabel *m_matchLabel = nullptr;

    QTableWidget *m_rules = nullptr;
    QToolButton *m_addRule = nullptr;
    QToolButton *m_removeRule = nullptr;

    /// \brief Признак программного заполнения таблицы — гасит обработчик изменений.
    bool m_populating = false;

    /// \brief Последний собранный набор правил.
    HighlightRules m_currentRules;
};

} // namespace spotty
