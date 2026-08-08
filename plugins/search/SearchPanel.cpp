/**
 * \file SearchPanel.cpp
 * \brief Реализация spotty::SearchPanel.
 */
#include "SearchPanel.h"

#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
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

// Ключи без префикса: пространство `plugins/search/` подставляет хост, и залезть в чужую
// настройку, ошибившись в строке, невозможно.
constexpr auto kKeyRules = "highlightRules";
constexpr auto kKeyRegex = "regularExpression";
constexpr auto kKeyCase = "caseSensitive";
constexpr auto kKeyWholeWords = "wholeWords";

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

/// \brief Квадратик выбранного цвета с тонкой обводкой.
QIcon colorSwatch(const QColor &color)
{
    constexpr int kSwatch = 14;

    QPixmap pixmap(kSwatch, kSwatch);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    // Обводка обязательна: без неё тёмный цвет на тёмной теме сливается с фоном ячейки, и
    // квадратик неотличим от пустого места — ровно та неисправность, которую чиним.
    painter.setPen(QPen(QColor(0x80, 0x80, 0x80), 1));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0.5, 0.5, kSwatch - 1, kSwatch - 1), 2, 2);

    return QIcon(pixmap);
}

} // namespace

SearchPanel::SearchPanel(IPanelHost *panelHost, QWidget *parent)
    : PanelWidget(panelHost, parent)
{
    setPanelTitle(tr("Search"));
    QVBoxLayout *layout = content();

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

    connect(m_pattern, &QLineEdit::textChanged, this, &SearchPanel::applySearch);
    connect(m_pattern, &QLineEdit::returnPressed, this, [this] { host()->findNext(); });

    const auto onOptionChanged = [this] {
        if (m_populating)
            return;
        host()->setValue(QLatin1String(kKeyRegex), m_regex->isChecked());
        host()->setValue(QLatin1String(kKeyCase), m_caseSensitive->isChecked());
        host()->setValue(QLatin1String(kKeyWholeWords), m_wholeWords->isChecked());
        applySearch();
    };
    connect(m_regex, &QCheckBox::toggled, this, onOptionChanged);
    connect(m_caseSensitive, &QCheckBox::toggled, this, onOptionChanged);
    connect(m_wholeWords, &QCheckBox::toggled, this, onOptionChanged);

    connect(m_filter, &QCheckBox::toggled, this,
            [this](bool on) { host()->setFilterEnabled(on); });
    connect(m_previous, &QToolButton::clicked, this, [this] { host()->findPrevious(); });
    connect(m_next, &QToolButton::clicked, this, [this] { host()->findNext(); });

    connect(host(), &IPanelHost::matchCountChanged, this, &SearchPanel::setMatchCount);

    // Единственное сочетание панели: фокус в поле поиска. Прежде окно знало о нём само и
    // переключало страницу по жёсткому номеру 2.
    host()->setShortcuts({PanelShortcut{
        .id = QStringLiteral("focus"),
        .title = tr("Find..."),
        .defaultSequence = QKeySequence::Find,
    }});
    connect(host(), &IPanelHost::shortcutActivated, this, [this](const QString &id) {
        if (!id.endsWith(QLatin1String(".focus")))
            return;
        host()->activatePanel(QStringLiteral("search"));
        focusSearch();
    });

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
        const QColor chosen = QColorDialog::getColor(
            toColor(item->data(Qt::UserRole).toUInt()), this, tr("Highlight colour"));
        if (!chosen.isValid())
            return;
        item->setIcon(colorSwatch(chosen));
        item->setData(Qt::UserRole, fromColor(chosen));
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

    updateIcons();
    reloadFromSettings();
}

void SearchPanel::updateIcons()
{
    m_previous->setIcon(host()->icon(mdi::ChevronUp, kToolGlyphSize));
    m_next->setIcon(host()->icon(mdi::ChevronDown, kToolGlyphSize));
    m_addRule->setIcon(host()->icon(mdi::Plus, kToolGlyphSize));
    m_removeRule->setIcon(host()->icon(mdi::Delete, kToolGlyphSize));
}

void SearchPanel::themeChanged()
{
    updateIcons();
}

void SearchPanel::settingsReset()
{
    reloadFromSettings();
}

void SearchPanel::reloadFromSettings()
{
    // Флаг гасит обработчики: иначе установка состояния флажка тут же перезаписала бы
    // настройку значением флажка, и чтение отменило бы само себя.
    m_populating = true;
    m_regex->setChecked(host()->value(QLatin1String(kKeyRegex), false).toBool());
    m_caseSensitive->setChecked(host()->value(QLatin1String(kKeyCase), false).toBool());
    m_wholeWords->setChecked(host()->value(QLatin1String(kKeyWholeWords), false).toBool());
    m_populating = false;

    // Правила восстанавливаются при запуске: их настраивают один раз под конкретное
    // устройство и ждут, что они останутся. Отдать их терминалу можно сразу — он
    // существует раньше панели, и обходной путь с геттером больше не нужен.
    m_currentRules.fromVariant(host()->value(QLatin1String(kKeyRules)).toList());
    populateRules(m_currentRules);
    host()->setHighlightRules(m_currentRules);
    applySearch();
}

void SearchPanel::applySearch()
{
    if (m_populating)
        return;
    host()->setSearchPattern(m_pattern->text(),
                             SearchOptions{
                                 .regularExpression = m_regex->isChecked(),
                                 .caseSensitive = m_caseSensitive->isChecked(),
                                 .wholeWords = m_wholeWords->isChecked(),
                             });
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

    // Цвет показывается значком, а не фоном ячейки.
    //
    // setBackground() здесь не работает вовсе: правило `QTableView::item` из таблицы
    // стилей задаёт фон само и отменяет то, что выставлено моделью. Та же ловушка, что с
    // флажком, — взяв элемент под свой стиль, изображение обязан дать сам. Ячейка
    // выглядела пустой, и выбранный цвет нельзя было увидеть, не открыв диалог заново.
    //
    // Значение при этом хранится в UserRole: восстанавливать цвет из пикселей значка
    // можно, но это чтение того, что мы сами же и нарисовали.
    auto *color = new QTableWidgetItem;
    color->setIcon(colorSwatch(toColor(rule.color)));
    color->setData(Qt::UserRole, rule.color);
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
        rule.color = color->data(Qt::UserRole).toUInt();
        rules.append(rule);
    }

    m_currentRules.setRules(rules);

    host()->setValue(QLatin1String(kKeyRules), m_currentRules.toVariant());
    host()->setHighlightRules(m_currentRules);
}

} // namespace spotty
