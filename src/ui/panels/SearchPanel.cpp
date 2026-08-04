/**
 * \file SearchPanel.cpp
 * \brief Реализация spotty::SearchPanel.
 */
#include "SearchPanel.h"

#include "../AppContext.h"
#include "../theme/MdiIcons.h"
#include "../theme/ThemeManager.h"

#include <settings/SettingsStore.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr int kToolGlyphSize = 16;

constexpr auto kKeyRules = "search/highlightRules";
constexpr auto kKeyRegex = "search/regularExpression";
constexpr auto kKeyCase = "search/caseSensitive";
constexpr auto kKeyWholeWords = "search/wholeWords";

/// \brief Колонки таблицы правил.
enum RuleColumn {
    ColumnEnabled = 0,
    ColumnPattern,
    ColumnColor,
    ColumnCount,
};

QColor toColor(quint32 rgb)
{
    return QColor(int((rgb >> 16) & 0xFF), int((rgb >> 8) & 0xFF), int(rgb & 0xFF));
}

quint32 fromColor(const QColor &color)
{
    return (quint32(color.red()) << 16) | (quint32(color.green()) << 8) | quint32(color.blue());
}

} // namespace

SearchPanel::SearchPanel(const AppContext &context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
{
    auto *layout = new QVBoxLayout(this);
    // Тот же отступ, что и в остальных боковых панелях — см. комментарий в MacrosPanel.
    layout->setContentsMargins(6, 10, 6, 10);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("Search"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(title);

    // --- Строка поиска ---------------------------------------------------------------

    m_pattern = new QLineEdit(this);
    m_pattern->setPlaceholderText(tr("Find in output"));
    m_pattern->setClearButtonEnabled(true);
    layout->addWidget(m_pattern);

    auto *navigationRow = new QHBoxLayout;
    navigationRow->setSpacing(4);

    m_previous = new QToolButton(this);
    m_previous->setAutoRaise(true);
    m_previous->setToolTip(tr("Previous match"));

    m_next = new QToolButton(this);
    m_next->setAutoRaise(true);
    m_next->setToolTip(tr("Next match"));

    m_matchLabel = new QLabel(this);
    m_matchLabel->setObjectName(QStringLiteral("hintLabel"));

    navigationRow->addWidget(m_previous);
    navigationRow->addWidget(m_next);
    navigationRow->addWidget(m_matchLabel, 1);
    layout->addLayout(navigationRow);

    m_regex = new QCheckBox(tr("Regular expression"), this);
    m_caseSensitive = new QCheckBox(tr("Case sensitive"), this);
    m_wholeWords = new QCheckBox(tr("Whole words"), this);
    m_filter = new QCheckBox(tr("Show only matching lines"), this);
    m_filter->setToolTip(tr("Hides everything that does not match, instead of just "
                            "highlighting it."));

    m_regex->setChecked(m_context.settings->value(QLatin1String(kKeyRegex), false).toBool());
    m_caseSensitive->setChecked(
        m_context.settings->value(QLatin1String(kKeyCase), false).toBool());
    m_wholeWords->setChecked(
        m_context.settings->value(QLatin1String(kKeyWholeWords), false).toBool());

    layout->addWidget(m_regex);
    layout->addWidget(m_caseSensitive);
    layout->addWidget(m_wholeWords);
    layout->addWidget(m_filter);

    // --- Правила подсветки -----------------------------------------------------------

    auto *rulesTitle = new QLabel(tr("Highlight rules"), this);
    rulesTitle->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(rulesTitle);

    m_rules = new QTableWidget(0, ColumnCount, this);
    m_rules->setHorizontalHeaderLabels({QString(), tr("Pattern"), tr("Colour")});
    m_rules->horizontalHeader()->setSectionResizeMode(ColumnPattern, QHeaderView::Stretch);
    m_rules->horizontalHeader()->setSectionResizeMode(ColumnEnabled,
                                                      QHeaderView::ResizeToContents);
    m_rules->horizontalHeader()->setSectionResizeMode(ColumnColor,
                                                      QHeaderView::ResizeToContents);
    m_rules->verticalHeader()->setVisible(false);
    m_rules->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rules->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_rules, 1);

    auto *ruleButtons = new QHBoxLayout;
    ruleButtons->setSpacing(4);

    m_addRule = new QToolButton(this);
    m_addRule->setAutoRaise(true);
    m_addRule->setToolTip(tr("Add rule"));

    m_removeRule = new QToolButton(this);
    m_removeRule->setAutoRaise(true);
    m_removeRule->setToolTip(tr("Delete rule"));

    ruleButtons->addWidget(m_addRule);
    ruleButtons->addWidget(m_removeRule);
    ruleButtons->addStretch(1);
    layout->addLayout(ruleButtons);

    // --- Связывание ------------------------------------------------------------------

    connect(m_pattern, &QLineEdit::textChanged, this, &SearchPanel::emitSearch);
    connect(m_pattern, &QLineEdit::returnPressed, this, &SearchPanel::findNextRequested);

    const auto onOptionChanged = [this] {
        m_context.settings->setValue(QLatin1String(kKeyRegex), m_regex->isChecked());
        m_context.settings->setValue(QLatin1String(kKeyCase), m_caseSensitive->isChecked());
        m_context.settings->setValue(QLatin1String(kKeyWholeWords), m_wholeWords->isChecked());
        emitSearch();
    };
    connect(m_regex, &QCheckBox::toggled, this, onOptionChanged);
    connect(m_caseSensitive, &QCheckBox::toggled, this, onOptionChanged);
    connect(m_wholeWords, &QCheckBox::toggled, this, onOptionChanged);

    connect(m_filter, &QCheckBox::toggled, this, &SearchPanel::filterToggled);
    connect(m_previous, &QToolButton::clicked, this, &SearchPanel::findPreviousRequested);
    connect(m_next, &QToolButton::clicked, this, &SearchPanel::findNextRequested);

    connect(m_rules, &QTableWidget::itemChanged, this, [this] {
        if (!m_populating)
            commitRules();
    });

    // Цвет правится щелчком по ячейке цвета: отдельная кнопка ради одного действия
    // загромождала бы панель.
    connect(m_rules, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column != ColumnColor)
            return;
        QTableWidgetItem *item = m_rules->item(row, ColumnColor);
        if (!item)
            return;
        const QColor chosen = QColorDialog::getColor(item->background().color(), this,
                                                     tr("Highlight colour"));
        if (!chosen.isValid())
            return;
        item->setBackground(chosen);
        commitRules();
    });

    connect(m_addRule, &QToolButton::clicked, this, [this] {
        HighlightRule rule;
        rule.pattern = QStringLiteral("ERROR");
        rule.color = 0xD26B6B;
        appendRuleRow(rule);
        commitRules();
    });

    connect(m_removeRule, &QToolButton::clicked, this, [this] {
        const int row = m_rules->currentRow();
        if (row < 0)
            return;
        m_rules->removeRow(row);
        commitRules();
    });

    const auto updateIcons = [this] {
        m_previous->setIcon(MdiIcons::icon(mdi::ChevronUp, kToolGlyphSize));
        m_next->setIcon(MdiIcons::icon(mdi::ChevronDown, kToolGlyphSize));
        m_addRule->setIcon(MdiIcons::icon(mdi::Plus, kToolGlyphSize));
        m_removeRule->setIcon(MdiIcons::icon(mdi::Delete, kToolGlyphSize));
    };
    if (m_context.theme)
        connect(m_context.theme, &ThemeManager::themeChanged, this, updateIcons);
    updateIcons();

    // Правила восстанавливаются при запуске: их настраивают один раз под конкретное
    // устройство и ждут, что они останутся.
    //
    // Сигнал здесь испускать бессмысленно — владелец ещё не успел к нему подключиться.
    // Начальное состояние он заберёт через highlightRules().
    m_currentRules.fromVariant(m_context.settings->value(QLatin1String(kKeyRules)).toList());
    populateRules(m_currentRules);
}

void SearchPanel::emitSearch()
{
    Q_EMIT searchChanged(m_pattern->text(), m_regex->isChecked(),
                         m_caseSensitive->isChecked(), m_wholeWords->isChecked());
}

void SearchPanel::setMatchCount(int count)
{
    if (m_pattern->text().isEmpty()) {
        m_matchLabel->clear();
        return;
    }
    m_matchLabel->setText(tr("%n line(s)", nullptr, count));
}

void SearchPanel::focusSearch()
{
    m_pattern->setFocus();
    m_pattern->selectAll();
}

void SearchPanel::appendRuleRow(const HighlightRule &rule)
{
    m_populating = true;

    const int row = m_rules->rowCount();
    m_rules->insertRow(row);

    auto *enabled = new QTableWidgetItem;
    enabled->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    enabled->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_rules->setItem(row, ColumnEnabled, enabled);

    m_rules->setItem(row, ColumnPattern, new QTableWidgetItem(rule.pattern));

    auto *color = new QTableWidgetItem;
    color->setBackground(toColor(rule.color));
    color->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    color->setToolTip(tr("Double-click to change"));
    m_rules->setItem(row, ColumnColor, color);

    m_populating = false;
}

void SearchPanel::populateRules(const HighlightRules &rules)
{
    m_populating = true;
    m_rules->setRowCount(0);
    m_populating = false;

    for (const HighlightRule &rule : rules.rules())
        appendRuleRow(rule);
}

void SearchPanel::commitRules()
{
    QList<HighlightRule> rules;
    rules.reserve(m_rules->rowCount());

    for (int row = 0; row < m_rules->rowCount(); ++row) {
        const QTableWidgetItem *enabled = m_rules->item(row, ColumnEnabled);
        const QTableWidgetItem *pattern = m_rules->item(row, ColumnPattern);
        const QTableWidgetItem *color = m_rules->item(row, ColumnColor);
        if (!enabled || !pattern || !color)
            continue;

        HighlightRule rule;
        rule.enabled = enabled->checkState() == Qt::Checked;
        rule.pattern = pattern->text();
        rule.color = fromColor(color->background().color());
        rules.append(rule);
    }

    m_currentRules.setRules(rules);

    m_context.settings->setValue(QLatin1String(kKeyRules), m_currentRules.toVariant());
    Q_EMIT highlightRulesChanged(m_currentRules);
}

} // namespace spotty
