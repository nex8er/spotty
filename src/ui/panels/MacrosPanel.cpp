/**
 * \file MacrosPanel.cpp
 * \brief Реализация spotty::MacrosPanel.
 */
#include "MacrosPanel.h"

#include "../AppContext.h"
#include "../dialogs/MacroEditDialog.h"
#include "../theme/MdiIcons.h"
#include "../theme/ThemeManager.h"

#include <settings/Paths.h>
#include <settings/SettingsStore.h>

#include <QComboBox>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr int kToolGlyphSize = 16;

constexpr auto kKeyPreset = "macros/preset";

/**
 * \brief Шкала периодов повторной отправки.
 *
 * Логарифмическая: между 1 мс и 60 с шестьдесят тысяч шагов, и равномерная шкала была бы
 * бесполезна. Значения подобраны так, чтобы покрыть и опрос датчика каждые несколько
 * миллисекунд, и «раз в минуту».
 */
constexpr int kPeriodsMs[] = {1,    2,    5,     10,    25,    50,    100,
                              250,  500,  1000,  2000,  5000,  10000, 30000, 60000};

/// \brief Подпись периода: миллисекунды до секунды, дальше секунды.
QString periodLabel(int milliseconds)
{
    if (milliseconds < 1000)
        return MacrosPanel::tr("%1 ms").arg(milliseconds);
    return MacrosPanel::tr("%1 s").arg(double(milliseconds) / 1000.0, 0, 'g', 3);
}

} // namespace

MacrosPanel::MacrosPanel(const AppContext &context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_store(Paths::macrosDir())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("Macros"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(title);

    // --- Пресеты ---------------------------------------------------------------------

    auto *presetRow = new QHBoxLayout;
    presetRow->setSpacing(4);

    m_presetCombo = new QComboBox(this);
    m_presetCombo->setToolTip(tr("Macro preset; each preset is a separate file"));

    m_addPresetButton = new QToolButton(this);
    m_addPresetButton->setAutoRaise(true);
    m_addPresetButton->setToolTip(tr("New preset"));

    m_removePresetButton = new QToolButton(this);
    m_removePresetButton->setAutoRaise(true);
    m_removePresetButton->setToolTip(tr("Delete preset"));

    presetRow->addWidget(m_presetCombo, 1);
    presetRow->addWidget(m_addPresetButton);
    presetRow->addWidget(m_removePresetButton);
    layout->addLayout(presetRow);

    // --- Список макросов -------------------------------------------------------------

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(false);
    layout->addWidget(m_list, 1);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(4);

    m_addButton = new QToolButton(this);
    m_addButton->setAutoRaise(true);
    m_addButton->setToolTip(tr("Add macro"));

    m_editButton = new QToolButton(this);
    m_editButton->setAutoRaise(true);
    m_editButton->setToolTip(tr("Edit macro"));

    m_removeButton = new QToolButton(this);
    m_removeButton->setAutoRaise(true);
    m_removeButton->setToolTip(tr("Delete macro"));

    buttonRow->addWidget(m_addButton);
    buttonRow->addWidget(m_editButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    // --- Периодическая отправка ------------------------------------------------------

    auto *periodicBox = new QGroupBox(tr("Repeat"), this);
    auto *periodicLayout = new QVBoxLayout(periodicBox);
    periodicLayout->setSpacing(6);

    m_periodicMacro = new QComboBox(periodicBox);

    m_periodInterval = new QComboBox(periodicBox);
    for (const int period : kPeriodsMs)
        m_periodInterval->addItem(periodLabel(period), period);
    m_periodInterval->setCurrentIndex(9); // 1 с

    auto *periodRow = new QHBoxLayout;
    periodRow->setSpacing(4);
    periodRow->addWidget(m_periodInterval, 1);

    m_periodicButton = new QPushButton(tr("Start"), periodicBox);
    m_periodicButton->setCheckable(true);
    periodRow->addWidget(m_periodicButton);

    m_actualLabel = new QLabel(periodicBox);
    m_actualLabel->setObjectName(QStringLiteral("hintLabel"));

    periodicLayout->addWidget(m_periodicMacro);
    periodicLayout->addLayout(periodRow);
    periodicLayout->addWidget(m_actualLabel);
    layout->addWidget(periodicBox);

    // --- Таймер ----------------------------------------------------------------------

    m_periodicTimer = new QTimer(this);
    // Точный таймер: на периодах в единицы миллисекунд огрубление по умолчанию превратило
    // бы заданный период во что угодно.
    m_periodicTimer->setTimerType(Qt::PreciseTimer);
    connect(m_periodicTimer, &QTimer::timeout, this, [this] {
        sendMacro(m_periodicMacro->currentIndex());
        ++m_periodicCount;
        updateActualInterval();
    });

    // --- Связывание ------------------------------------------------------------------

    connect(m_presetCombo, &QComboBox::currentTextChanged, this, [this](const QString &name) {
        if (name.isEmpty())
            return;
        m_store.loadPreset(name);
        m_context.settings->setValue(QLatin1String(kKeyPreset), name);
        reloadMacros();
    });

    connect(m_addPresetButton, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New preset"), tr("Name"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (!ok || name.isEmpty())
            return;
        if (!m_store.createPreset(name.trimmed())) {
            QMessageBox::warning(this, tr("New preset"),
                                 tr("Could not create a preset with that name."));
            return;
        }
        reloadPresets();
        m_presetCombo->setCurrentText(name.trimmed());
    });

    connect(m_removePresetButton, &QToolButton::clicked, this, [this] {
        const QString name = m_presetCombo->currentText();
        if (name.isEmpty())
            return;
        if (QMessageBox::question(this, tr("Delete preset"),
                                  tr("Delete preset \"%1\" and its file?").arg(name))
            != QMessageBox::Yes) {
            return;
        }
        m_store.deletePreset(name);
        reloadPresets();
    });

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        sendMacro(m_list->row(item));
    });
    connect(m_addButton, &QToolButton::clicked, this, &MacrosPanel::addMacro);
    connect(m_editButton, &QToolButton::clicked, this, [this] {
        editMacro(m_list->currentItem());
    });
    connect(m_removeButton, &QToolButton::clicked, this, &MacrosPanel::removeMacro);

    connect(m_periodicButton, &QPushButton::toggled, this, [this](bool on) {
        if (on)
            startPeriodic();
        else
            stopPeriodic();
    });

    if (m_context.theme) {
        connect(m_context.theme, &ThemeManager::themeChanged, this, [this] {
            m_addPresetButton->setIcon(MdiIcons::icon(mdi::Plus, kToolGlyphSize));
            m_removePresetButton->setIcon(MdiIcons::icon(mdi::Minus, kToolGlyphSize));
            m_addButton->setIcon(MdiIcons::icon(mdi::Plus, kToolGlyphSize));
            m_editButton->setIcon(MdiIcons::icon(mdi::Pencil, kToolGlyphSize));
            m_removeButton->setIcon(MdiIcons::icon(mdi::Delete, kToolGlyphSize));
        });
    }
    m_addPresetButton->setIcon(MdiIcons::icon(mdi::Plus, kToolGlyphSize));
    m_removePresetButton->setIcon(MdiIcons::icon(mdi::Minus, kToolGlyphSize));
    m_addButton->setIcon(MdiIcons::icon(mdi::Plus, kToolGlyphSize));
    m_editButton->setIcon(MdiIcons::icon(mdi::Pencil, kToolGlyphSize));
    m_removeButton->setIcon(MdiIcons::icon(mdi::Delete, kToolGlyphSize));

    reloadPresets();
    setSendEnabled(false);
}

MacrosPanel::~MacrosPanel() = default;

void MacrosPanel::reloadPresets()
{
    const QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();

    QStringList presets = m_store.presets();
    if (presets.isEmpty()) {
        // Первый запуск: пустой набор лучше пустого списка — пользователю есть куда
        // добавить первый макрос.
        m_store.createPreset(MacroStore::defaultPresetName());
        presets = m_store.presets();
    }
    m_presetCombo->addItems(presets);

    const QString remembered =
        m_context.settings->value(QLatin1String(kKeyPreset)).toString();
    const int index = m_presetCombo->findText(remembered);
    m_presetCombo->setCurrentIndex(index >= 0 ? index : 0);

    m_store.loadPreset(m_presetCombo->currentText());
    reloadMacros();
}

void MacrosPanel::reloadMacros()
{
    m_list->clear();
    m_periodicMacro->clear();

    for (const Macro &macro : m_store.macros()) {
        QString label = macro.name.isEmpty() ? macro.payload : macro.name;
        if (!macro.shortcut.isEmpty())
            label += QStringLiteral("   [%1]").arg(macro.shortcut);

        auto *item = new QListWidgetItem(label, m_list);
        item->setToolTip(QStringLiteral("%1\n%2 · %3")
                             .arg(macro.payload,
                                  DataCodec::formatName(macro.format),
                                  DataCodec::terminationName(macro.termination)));

        m_periodicMacro->addItem(macro.name.isEmpty() ? macro.payload : macro.name);
    }

    rebuildShortcuts();
    m_periodicButton->setEnabled(m_sendEnabled && !m_store.macros().isEmpty());
}

void MacrosPanel::rebuildShortcuts()
{
    qDeleteAll(m_shortcuts);
    m_shortcuts.clear();

    const QList<Macro> &macros = m_store.macros();
    for (int i = 0; i < macros.size(); ++i) {
        const QString sequence = macros.at(i).shortcut;
        if (sequence.isEmpty())
            continue;

        // Область действия — всё окно: макрос должен срабатывать и когда фокус в строке
        // отправки или в терминале, иначе горячая клавиша почти бесполезна.
        auto *shortcut = new QShortcut(QKeySequence(sequence), window());
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, i] { sendMacro(i); });
        m_shortcuts.append(shortcut);
    }
}

void MacrosPanel::sendMacro(int index)
{
    if (index < 0 || index >= m_store.macros().size())
        return;
    if (!m_sendEnabled) {
        Q_EMIT statusMessage(tr("The interface is not open."));
        return;
    }

    QString error;
    const QByteArray data = m_store.macros().at(index).encode(&error);
    if (!error.isEmpty()) {
        Q_EMIT statusMessage(tr("Macro \"%1\": %2")
                                 .arg(m_store.macros().at(index).name, error));
        return;
    }
    if (data.isEmpty())
        return;

    Q_EMIT sendRequested(data);
}

void MacrosPanel::addMacro()
{
    Macro macro;
    MacroEditDialog dialog(macro, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_store.macros().append(dialog.macro());
    m_store.save();
    reloadMacros();
}

void MacrosPanel::editMacro(QListWidgetItem *item)
{
    if (!item)
        return;

    const int row = m_list->row(item);
    if (row < 0 || row >= m_store.macros().size())
        return;

    MacroEditDialog dialog(m_store.macros().at(row), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_store.macros()[row] = dialog.macro();
    m_store.save();
    reloadMacros();
}

void MacrosPanel::removeMacro()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_store.macros().size())
        return;

    m_store.macros().removeAt(row);
    m_store.save();
    reloadMacros();
}

void MacrosPanel::startPeriodic()
{
    if (m_periodicMacro->currentIndex() < 0 || !m_sendEnabled) {
        m_periodicButton->setChecked(false);
        return;
    }

    m_periodicStartedMs = QDateTime::currentMSecsSinceEpoch();
    m_periodicCount = 0;

    m_periodicTimer->start(m_periodInterval->currentData().toInt());
    m_periodicButton->setText(tr("Stop"));
}

void MacrosPanel::stopPeriodic()
{
    m_periodicTimer->stop();
    m_periodicButton->setText(tr("Start"));
    m_actualLabel->clear();
}

void MacrosPanel::updateActualInterval()
{
    const int requested = m_periodInterval->currentData().toInt();

    // Показываем фактический период только там, где он расходится с заданным: на
    // периодах от 10 мс таймер попадает точно, и лишняя цифра только отвлекала бы.
    if (requested >= 10 || m_periodicCount < 10)
        return;

    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_periodicStartedMs;
    const double actual = double(elapsed) / m_periodicCount;

    m_actualLabel->setText(tr("actual: %1 ms").arg(actual, 0, 'f', 1));
}

void MacrosPanel::setSendEnabled(bool enabled)
{
    m_sendEnabled = enabled;
    m_periodicButton->setEnabled(enabled && !m_store.macros().isEmpty());

    if (!enabled && m_periodicButton->isChecked())
        m_periodicButton->setChecked(false);
}

} // namespace spotty
