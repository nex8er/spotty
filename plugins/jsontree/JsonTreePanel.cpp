/**
 * \file JsonTreePanel.cpp
 * \brief Реализация spotty::JsonTreePanel.
 */
#include "JsonTreePanel.h"

#include "JsonRateDelegate.h"
#include "JsonTreeView.h"

#include <spotty/data/JsonFramer.h>
#include <spotty/data/JsonTreeModel.h>
#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

namespace spotty {

namespace {

constexpr int kToolGlyphSize = 16;

// Ключи настроек вида; должны совпадать со схемой в JsonTreePlugin::settingsSchema().
constexpr auto kKeyFlash = "flashOnChange";
constexpr auto kKeyFlashMs = "flashMs";

/// \brief Готовые длительности вспышки в меню правой кнопки, мс.
constexpr int kFlashPresets[] = {100, 200, 400, 700, 1200};

qint64 monotonicNow()
{
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.nsecsElapsed();
}

} // namespace

JsonTreePanel::JsonTreePanel(IPanelHost *panelHost, JsonTreeModel *model, JsonFramer *framer,
                             QWidget *parent)
    : PanelWidget(panelHost, parent)
    , m_model(model)
    , m_framer(framer)
{
    setPanelTitle(tr("JSON"));

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(host()->metric(IPanelHost::Metric::Gap));

    const auto makeButton = [this](char32_t glyph, const QString &tip, bool checkable) {
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setCheckable(checkable);
        button->setIconSize(QSize(kToolGlyphSize, kToolGlyphSize));
        button->setIcon(host()->icon(glyph, kToolGlyphSize));
        button->setToolTip(tip);
        return button;
    };

    m_pauseButton = makeButton(mdi::Pause, tr("Pause parsing"), true);
    m_clearButton = makeButton(mdi::Broom, tr("Clear the tree"), false);
    m_hideStaleButton = makeButton(mdi::FilterOff, tr("Hide fields that stopped arriving"), true);
    m_pruneButton = makeButton(mdi::Delete, tr("Remove fields that stopped arriving"), false);
    m_expandButton = makeButton(mdi::ChevronDown, tr("Expand everything"), false);
    m_collapseButton = makeButton(mdi::ChevronUp, tr("Collapse everything"), false);
    m_flashButton = makeButton(mdi::Flash, tr("Flash a field when its value changes\n"
                                              "Right-click to set how long it glows"), true);
    m_flashButton->setContextMenuPolicy(Qt::CustomContextMenu);

    buttons->addWidget(m_pauseButton);
    buttons->addWidget(m_clearButton);
    buttons->addWidget(m_hideStaleButton);
    buttons->addWidget(m_pruneButton);
    buttons->addWidget(m_expandButton);
    buttons->addWidget(m_collapseButton);
    buttons->addWidget(m_flashButton);
    buttons->addStretch(1);
    content()->addLayout(buttons);

    // Полоса предупреждения о достигнутом пределе: в девяти случаях из десяти причина в
    // незаданном ключе идентификации массива, поэтому она названа прямо здесь.
    m_warning = new QLabel(this);
    m_warning->setObjectName(QStringLiteral("hintLabel"));
    m_warning->setWordWrap(true);
    m_warning->hide();
    content()->addWidget(m_warning);

    m_view = new JsonTreeView(host(), m_model, this);
    content()->addWidget(m_view, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("hintLabel"));
    content()->addWidget(m_status);

    connect(m_pauseButton, &QToolButton::toggled, this, [this](bool on) {
        m_paused = on;
        // Значок отражает действие, которое кнопка выполнит, а не текущее состояние.
        m_pauseButton->setIcon(host()->icon(on ? mdi::Play : mdi::Pause, kToolGlyphSize));
        m_pauseButton->setToolTip(on ? tr("Resume parsing") : tr("Pause parsing"));
        Q_EMIT pauseChanged(on);
    });

    connect(m_clearButton, &QToolButton::clicked, this, [this] {
        m_model->clear();
        m_framer->reset();
        updateStatus();
    });

    connect(m_hideStaleButton, &QToolButton::toggled, this,
            [this](bool on) { m_view->setHideStale(on); });

    connect(m_pruneButton, &QToolButton::clicked, this, [this] {
        const int removed = int(m_model->pruneStale(monotonicNow()).size());
        host()->showStatusMessage(tr("%n field(s) removed", nullptr, removed));
        updateStatus();
    });

    connect(m_expandButton, &QToolButton::clicked, this, [this] { m_view->expandTree(); });
    connect(m_collapseButton, &QToolButton::clicked, this, [this] { m_view->collapseTree(); });

    connect(m_flashButton, &QToolButton::toggled, this, [this](bool on) {
        m_view->setFlashEnabled(on);
        host()->setValue(QLatin1String(kKeyFlash), on);
    });
    connect(m_flashButton, &QWidget::customContextMenuRequested,
            this, &JsonTreePanel::showFlashMenu);

    connect(m_view->tree(), &QTreeWidget::customContextMenuRequested,
            this, &JsonTreePanel::showTreeMenu);

    // Строка состояния — по таймеру: числа, меняющиеся чаще пяти раз в секунду, всё равно
    // не читаются, а QLabel::setText() на каждый документ означал бы перерисовку на каждую
    // строку устройства.
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(kStatusIntervalMs);
    connect(m_statusTimer, &QTimer::timeout, this, &JsonTreePanel::updateStatus);
    connect(m_model, &JsonTreeModel::changed, this, [this] {
        if (!m_statusTimer->isActive())
            m_statusTimer->start();
    });

    applyViewSettings();
    updateStatus();
}

void JsonTreePanel::themeChanged()
{
    updateIcons();
    m_view->applyTheme();
}

void JsonTreePanel::settingsReset()
{
    // Настройки модели применяет плагин: он подписан на тот же сигнал и владеет моделью,
    // которая живёт дольше панели. Здесь — только то, что относится к показу.
    applyViewSettings();
}

void JsonTreePanel::applyViewSettings()
{
    const bool flash = host()->value(QLatin1String(kKeyFlash), true).toBool();
    m_view->setFlashEnabled(flash);
    // Кнопка отражает то же состояние, что и настройка, но её собственный обработчик пишет
    // настройку обратно — без блокировки применение отменяло бы само себя.
    const QSignalBlocker blocker(m_flashButton);
    m_flashButton->setChecked(flash);

    m_view->setFlashDurationMs(host()->value(QLatin1String(kKeyFlashMs),
                                             JsonRateDelegate::kDefaultFlashMs).toInt());
}

void JsonTreePanel::setFlashDuration(int milliseconds)
{
    m_view->setFlashDurationMs(milliseconds);
    // Сохраняем зажатое видом значение, а не набранное: иначе в файле настроек осталось бы
    // число, которого панель не показывает.
    host()->setValue(QLatin1String(kKeyFlashMs), m_view->flashDurationMs());
}

void JsonTreePanel::showFlashMenu(const QPoint &position)
{
    QMenu menu(this);
    // Отметка напротив выбранного читается быстрее выпадающего списка — тот же приём, что
    // у формата и терминации в меню макроса.
    auto *group = new QActionGroup(&menu);
    group->setExclusive(true);

    const int current = m_view->flashDurationMs();
    bool matched = false;
    for (const int preset : kFlashPresets) {
        QAction *action = menu.addAction(tr("%1 ms").arg(preset));
        action->setCheckable(true);
        action->setChecked(preset == current);
        matched = matched || preset == current;
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, preset] { setFlashDuration(preset); });
    }

    menu.addSeparator();
    QAction *custom = menu.addAction(tr("Other…"));
    custom->setCheckable(true);
    custom->setChecked(!matched);
    group->addAction(custom);
    connect(custom, &QAction::triggered, this, [this] {
        bool ok = false;
        const int value = QInputDialog::getInt(window(), tr("Flash duration"),
                                               tr("Milliseconds:"), m_view->flashDurationMs(),
                                               JsonRateDelegate::kMinFlashMs,
                                               JsonRateDelegate::kMaxFlashMs, 10, &ok);
        if (ok)
            setFlashDuration(value);
    });

    menu.exec(m_flashButton->mapToGlobal(position));
}

void JsonTreePanel::updateIcons()
{
    m_pauseButton->setIcon(host()->icon(m_paused ? mdi::Play : mdi::Pause, kToolGlyphSize));
    m_clearButton->setIcon(host()->icon(mdi::Broom, kToolGlyphSize));
    m_hideStaleButton->setIcon(host()->icon(mdi::FilterOff, kToolGlyphSize));
    m_pruneButton->setIcon(host()->icon(mdi::Delete, kToolGlyphSize));
    m_expandButton->setIcon(host()->icon(mdi::ChevronDown, kToolGlyphSize));
    m_collapseButton->setIcon(host()->icon(mdi::ChevronUp, kToolGlyphSize));
    m_flashButton->setIcon(host()->icon(mdi::Flash, kToolGlyphSize));
}

void JsonTreePanel::updateStatus()
{
    const JsonFramer::Counters &counters = m_framer->counters();

    m_status->setText(tr("%1 fields · %2 documents").arg(m_model->nodeCount())
                          .arg(m_model->documents()));
    // Разбивка уходит в подсказку: в строке она не поместилась бы, а вопрос «почему в
    // дереве не то, что я жду» задают редко, но тогда нужны все числа сразу.
    m_status->setToolTip(tr("Documents: %1\nText lines skipped: %2\nMalformed: %3\n"
                            "Abandoned: %4\nPaths rejected by limits: %5")
                             .arg(counters.documents)
                             .arg(counters.textLines)
                             .arg(counters.malformed)
                             .arg(counters.abandoned)
                             .arg(m_model->rejectedPaths()));

    if (m_model->truncated()) {
        m_warning->setText(tr("Node limit reached (%1). New fields are ignored. If the "
                              "stream contains arrays, set the array identity field in "
                              "settings — otherwise every element makes its own branch.")
                               .arg(m_model->maxNodes()));
        m_warning->show();
    } else {
        m_warning->hide();
    }
}

void JsonTreePanel::showTreeMenu(const QPoint &position)
{
    const int node = m_view->nodeAt(position);
    if (node < 0 || !m_model->isValidNode(node))
        return;

    QMenu menu(this);
    menu.addAction(tr("Copy path"), this, [this, node] {
        QApplication::clipboard()->setText(m_model->path(node));
    });
    menu.addAction(tr("Copy value"), this, [this, node] {
        QApplication::clipboard()->setText(m_model->node(node).value);
    });
    menu.addSeparator();
    menu.addAction(tr("Expand everything below"), this, [this, position] {
        if (QTreeWidgetItem *item = m_view->tree()->itemAt(position))
            m_view->tree()->expandRecursively(m_view->tree()->indexFromItem(item));
    });

    menu.exec(m_view->tree()->viewport()->mapToGlobal(position));
}

} // namespace spotty
