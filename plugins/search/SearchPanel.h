/**
 * \file SearchPanel.h
 * \brief Панель поиска и правил подсветки.
 */
#pragma once

#include <spotty/data/HighlightRules.h>
#include <spotty/ui/PanelWidget.h>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QToolButton;

namespace spotty {

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
 *
 * \note Прежде панель отдавала прочитанные правила наружу геттером: она читала их в
 *       конструкторе, когда подключаться к её сигналу было ещё некому, и владелец забирал
 *       начальное состояние отдельным вызовом после связывания. С хостом обходного пути
 *       не нужно — терминал существует раньше панели, и правила отдаются сразу.
 */
class SearchPanel : public PanelWidget
{
    Q_OBJECT

public:
    explicit SearchPanel(IPanelHost *host, QWidget *parent = nullptr);

    /// \brief Передать фокус полю поиска. Зовётся по сочетанию клавиш.
    void focusSearch();

protected:
    void themeChanged() override;
    void settingsReset() override;

private:
    /// \brief Собрать образец из полей и передать его терминалу.
    void applySearch();

    /// \brief Прочитать правила из таблицы, сохранить и передать терминалу.
    void commitRules();

    /// \brief Заполнить таблицу из набора правил.
    void populateRules(const HighlightRules &rules);

    /// \brief Добавить строку таблицы под одно правило.
    void appendRuleRow(const HighlightRule &rule);

    /// \brief Показать число найденных строк.
    void setMatchCount(int count);

    /// \brief Прочитать всё своё из настроек и применить.
    void reloadFromSettings();

    void updateIcons();

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
