/**
 * \file MacrosPanel.cpp
 * \brief Реализация spotty::MacrosPanel.
 */
#include "MacrosPanel.h"

#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include "ShortcutDelegate.h"


#include <QActionGroup>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QIntValidator>
#include <QItemSelectionModel>
#include <QMessageBox>

#include <algorithm>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr int kToolGlyphSize = 16;
constexpr int kSendGlyphSize = 18;

/// \brief Значок кнопки отправки в строке таблицы — крупнее общих значков панели,
/// это основное действие макроса и должно читаться первым взглядом.
constexpr int kRowSendGlyphSize = 20;

/// \brief Ширина колонки с кнопкой отправки, px.
///
/// Столбец задан фиксированным намеренно, а не `ResizeToContents`: та колонка содержит
/// не текстовый элемент, а виджет `QToolButton`, вставленный через `setCellWidget()`, и
/// пересчёт ширины по содержимому на такие ячейки надёжно не срабатывает — Qt пересчитывает
/// «по содержимому» через сигналы модели данных, а виджет в ячейке под неё не подпадает.
/// Итог без фиксации — щель немотивированной ширины между кнопкой и правым краем таблицы.
constexpr int kSendColumnWidth = 34;

// Ключ без префикса: пространство `plugins/macros/` подставляет хост.
constexpr auto kKeyPreset = "preset";

/**
 * \brief Шкала периодов повторной отправки.
 *
 * Логарифмическая: между 1 мс и 60 с шестьдесят тысяч шагов, и равномерная шкала была бы
 * бесполезна. Значения покрывают и опрос датчика каждые несколько миллисекунд, и
 * «раз в минуту».
 */
constexpr int kPeriodsMs[] = {1,   2,    5,    10,   25,    50,    100,
                              250, 500,  1000, 2000, 5000,  10000, 30000, 60000};

constexpr DataCodec::Format kFormats[] = {
    DataCodec::Format::Text,
    DataCodec::Format::Hex,
    DataCodec::Format::Base64,
};

constexpr DataCodec::Termination kTerminations[] = {
    DataCodec::Termination::None,
    DataCodec::Termination::Lf,
    DataCodec::Termination::Cr,
    DataCodec::Termination::CrLf,
    DataCodec::Termination::Nul,
};

/// \brief Подпись периода: миллисекунды до секунды, дальше секунды.
/// \name Пределы периода повторной отправки, мс
/// Ниже единицы период не имеет смысла — гранулярность таймеров операционной системы
/// начинается там же. Выше часа повтор перестаёт быть повтором.
/// @{
constexpr int kMinPeriodMs = 1;
constexpr int kMaxPeriodMs = 3'600'000;
/// @}

QString periodLabel(int milliseconds)
{
    if (milliseconds < 1000)
        return MacrosPanel::tr("%1 ms").arg(milliseconds);
    return MacrosPanel::tr("%1 s").arg(double(milliseconds) / 1000.0, 0, 'g', 3);
}

} // namespace

MacrosPanel::MacrosPanel(IPanelHost *panelHost, QWidget *parent)
    : PanelWidget(panelHost, parent)
    // Каталог даёт хост: `<конфигурация>/macros`. Тот же самый, что раньше возвращал
    // Paths::macrosDir(), — идентификатор плагина совпал с именем каталога, и пресеты
    // пользователя никуда не переехали.
    , m_store(panelHost->dataDir())
{
    setPanelTitle(tr("Macros"));
    QVBoxLayout *layout = content();

    // --- Наборы ----------------------------------------------------------------------

    auto *presetRow = new QHBoxLayout;
    // Тот же шаг, что и у остальных рядов с кнопками-значками в приложении — общий
    // IPanelHost::Metric::Gap, а не свой отдельный номер.
    presetRow->setSpacing(host()->metric(IPanelHost::Metric::Gap));

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

    // --- Таблица макросов -------------------------------------------------------------

    m_table = new QTableWidget(0, ColumnCount, this);
    // Заголовок скрыт, а не убран из модели: строки «Команда» / «Клавиша» не несут
    // ничего, чего не видно из самого содержимого столбцов, а место экономит панель и
    // без того нешироких боковых панелей. Режимы растяжения столбцов работают и без
    // видимого заголовка — они не привязаны к его отображению.
    m_table->horizontalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnCommand, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnShortcut,
                                                      QHeaderView::ResizeToContents);
    // Fixed, а не ResizeToContents: колонка несёт виджет кнопки, а не элемент модели, и
    // авторасчёт «по содержимому» на такие ячейки не срабатывает (см. kSendColumnWidth) —
    // без фиксации между кнопкой и правым краем таблицы оставалась немотивированная щель.
    m_table->horizontalHeader()->setSectionResizeMode(ColumnSend, QHeaderView::Fixed);
    m_table->setColumnWidth(ColumnSend, kSendColumnWidth);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Множественное выделение: удалять и выгружать по одному набор из двадцати команд
    // утомительно, а другого способа поделиться половиной набора нет.
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    // Делегат ловит настоящее нажатие: набирать «Ctrl+Shift+F5» посимвольно человек
    // ошибётся, а несуществующее сочетание так записать вовсе нельзя.
    m_table->setItemDelegateForColumn(ColumnShortcut, new ShortcutDelegate(this));
    layout->addWidget(m_table, 1);

    // --- Кнопки под таблицей, выровненные вправо ---------------------------------------

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(host()->metric(IPanelHost::Metric::Gap));
    buttonRow->addStretch(1);

    const auto makeButton = [this, buttonRow](const QString &tip) {
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setToolTip(tip);
        buttonRow->addWidget(button);
        return button;
    };

    m_addButton = makeButton(tr("Add macro"));
    m_removeButton = makeButton(tr("Delete macro"));
    m_exportButton = makeButton(tr("Export macros"));
    m_importButton = makeButton(tr("Import macros"));
    layout->addLayout(buttonRow);

    // --- Периодическая отправка --------------------------------------------------------

    auto *periodicBox = new QGroupBox(tr("Auto-send"), this);
    // Без этого групбокс — единственный виджет с политикой Preferred среди соседей
    // растянутой таблицы — забирал себе долю лишней высоты панели вместо неё: таблица
    // соседствует с ним в одном QVBoxLayout, и Qt отдавал остаток не только ей. Fixed
    // прибивает высоту ровно к содержимому.
    periodicBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *periodicLayout = new QVBoxLayout(periodicBox);
    periodicLayout->setSpacing(6);

    m_periodicMacro = new QComboBox(periodicBox);

    m_periodInterval = new QComboBox(periodicBox);
    for (const int period : kPeriodsMs)
        m_periodInterval->addItem(periodLabel(period), period);
    m_periodInterval->setCurrentIndex(9); // 1 с
    // Список остаётся, но становится редактируемым: пресеты покрывают обычные случаи, а
    // «каждые 1700 мс под цикл опроса устройства» в список не впишешь — таких значений
    // столько же, сколько устройств.
    m_periodInterval->setEditable(true);
    m_periodInterval->setObjectName(QStringLiteral("autoSendInterval"));
    m_periodInterval->setInsertPolicy(QComboBox::NoInsert);
    m_periodInterval->setValidator(
        new QIntValidator(kMinPeriodMs, kMaxPeriodMs, m_periodInterval));
    m_periodInterval->setToolTip(
        tr("Pick a preset or type a custom period in milliseconds"));

    m_periodicButton = new QToolButton(periodicBox);
    m_periodicButton->setCheckable(true);
    m_periodicButton->setAutoRaise(true);
    m_periodicButton->setIconSize(QSize(kSendGlyphSize, kSendGlyphSize));

    auto *periodRow = new QHBoxLayout;
    periodRow->setSpacing(host()->metric(IPanelHost::Metric::Gap));
    periodRow->addWidget(m_periodInterval, 1);
    periodRow->addWidget(m_periodicButton);

    m_actualLabel = new QLabel(periodicBox);
    m_actualLabel->setObjectName(QStringLiteral("hintLabel"));
    // Скрыт, а не просто пуст: пустой QLabel всё равно резервирует высоту строки шрифта,
    // и это резервирование под текст, который появляется лишь изредка (периоды короче
    // 10 мс), и было тем самым лишним местом под списком периода. Скрытый виджет Qt из
    // расчёта высоты компоновки исключает вовсе.
    m_actualLabel->hide();

    periodicLayout->addWidget(m_periodicMacro);
    periodicLayout->addLayout(periodRow);
    periodicLayout->addWidget(m_actualLabel);
    layout->addWidget(periodicBox);

    // --- Таймер -----------------------------------------------------------------------

    m_periodicTimer = new QTimer(this);
    // Точный таймер: на периодах в единицы миллисекунд огрубление по умолчанию превратило
    // бы заданный период во что угодно.
    m_periodicTimer->setTimerType(Qt::PreciseTimer);
    connect(m_periodicTimer, &QTimer::timeout, this, [this] {
        sendMacro(m_periodicMacro->currentIndex());
        ++m_periodicCount;
        updateActualInterval();
    });

    // --- Связывание -------------------------------------------------------------------

    connect(m_presetCombo, &QComboBox::currentTextChanged, this, [this](const QString &name) {
        if (name.isEmpty())
            return;
        m_store.loadPreset(name);
        host()->setValue(QLatin1String(kKeyPreset), name);
        reloadMacros();
    });

    connect(m_addPresetButton, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New preset"), tr("Name"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (!ok || name.trimmed().isEmpty())
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

    connect(m_table, &QTableWidget::itemChanged, this, [this] {
        if (!m_populating)
            commitTable();
    });
    connect(m_table, &QWidget::customContextMenuRequested,
            this, &MacrosPanel::showContextMenu);

    connect(m_addButton, &QToolButton::clicked, this, &MacrosPanel::addMacro);
    connect(m_removeButton, &QToolButton::clicked, this, &MacrosPanel::removeSelected);
    connect(m_exportButton, &QToolButton::clicked, this, &MacrosPanel::exportMacros);
    connect(m_importButton, &QToolButton::clicked, this, &MacrosPanel::importMacros);

    connect(m_periodicButton, &QToolButton::toggled, this, [this](bool on) {
        if (on)
            startPeriodic();
        else
            stopPeriodic();
    });

    // Обратное действие к «положить макрос в строку отправки»: набрал команду, проверил
    // её, сохранил как макрос — не переписывая заново в таблицу.
    connect(host(), &IPanelHost::sendBarSaveRequested, this,
            [this](const QString &text, DataCodec::Format format,
                   DataCodec::Termination termination) {
                Macro macro;
                macro.payload = text;
                macro.format = format;
                macro.termination = termination;
                macro.shortcut = m_store.suggestShortcut();

                m_store.macros().append(macro);
                m_store.save();
                reloadMacros();

                host()->activatePanel(QStringLiteral("macros"));
                host()->showStatusMessage(tr("Saved as a macro: %1").arg(text));
            });

    connect(host(), &IPanelHost::shortcutActivated, this, [this](const QString &id) {
        // Идентификаторы выданы в rebuildShortcuts() как "macros.macroN".
        static const QString prefix = QStringLiteral("macros.macro");
        if (!id.startsWith(prefix))
            return;
        bool ok = false;
        const int index = QStringView(id).mid(prefix.size()).toInt(&ok);
        if (ok)
            sendMacro(index);
    });

    updateIcons();
    reloadPresets();
    setSendEnabled(false);
}

MacrosPanel::~MacrosPanel() = default;

void MacrosPanel::themeChanged()
{
    updateIcons();
}

void MacrosPanel::channelStateChanged(ChannelState state)
{
    setSendEnabled(state == ChannelState::Open);
}

void MacrosPanel::settingsReset()
{
    reloadPresets();
}

void MacrosPanel::updateIcons()
{
    m_addPresetButton->setIcon(host()->icon(mdi::Plus, kToolGlyphSize));
    m_removePresetButton->setIcon(host()->icon(mdi::Minus, kToolGlyphSize));
    m_addButton->setIcon(host()->icon(mdi::Plus, kToolGlyphSize));
    m_removeButton->setIcon(host()->icon(mdi::Delete, kToolGlyphSize));
    m_exportButton->setIcon(host()->icon(mdi::FileExport, kToolGlyphSize));
    m_importButton->setIcon(host()->icon(mdi::FileImport, kToolGlyphSize));

    // Значок кнопки повтора отражает действие, которое она выполнит, а не текущее
    // состояние: во время отправки на ней «стоп».
    const bool running = m_periodicTimer && m_periodicTimer->isActive();
    m_periodicButton->setIcon(host()->icon(running ? mdi::Stop : mdi::Play, kSendGlyphSize));
    m_periodicButton->setToolTip(running ? tr("Stop repeating") : tr("Start repeating"));

    // Кнопки отправки в строках таблицы тоже перекрашиваются: они нарисованы значком.
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (auto *button = qobject_cast<QToolButton *>(m_table->cellWidget(row, ColumnSend)))
            button->setIcon(host()->icon(mdi::Send, kRowSendGlyphSize));
    }
}

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

    const QString remembered = host()->value(QLatin1String(kKeyPreset)).toString();
    const int index = m_presetCombo->findText(remembered);
    m_presetCombo->setCurrentIndex(index >= 0 ? index : 0);

    m_store.loadPreset(m_presetCombo->currentText());
    reloadMacros();
}

void MacrosPanel::fillRow(int row, const Macro &macro)
{
    auto *command = new QTableWidgetItem(macro.payload);
    command->setToolTip(tr("%1 · %2\nDouble-click to edit, right-click for options")
                            .arg(DataCodec::formatName(macro.format),
                                 DataCodec::terminationName(macro.termination)));
    m_table->setItem(row, ColumnCommand, command);

    auto *shortcut = new QTableWidgetItem(macro.shortcut);
    shortcut->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, ColumnShortcut, shortcut);

    // Кнопка отправки — виджет в ячейке, а не элемент: по элементу пришлось бы ловить
    // щелчок и отличать его от выделения строки.
    auto *send = new QToolButton(m_table);
    send->setAutoRaise(true);
    send->setToolTip(tr("Send now"));
    send->setIconSize(QSize(kRowSendGlyphSize, kRowSendGlyphSize));
    send->setIcon(host()->icon(mdi::Send, kRowSendGlyphSize));
    connect(send, &QToolButton::clicked, this, [this, send] {
        // Номер строки ищем на месте: строки удаляют и переставляют, и захваченный
        // индекс через минуту указывал бы на чужой макрос.
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->cellWidget(row, ColumnSend) == send) {
                sendMacro(row);
                return;
            }
        }
    });
    m_table->setCellWidget(row, ColumnSend, send);
}

void MacrosPanel::reloadMacros()
{
    m_populating = true;
    m_table->setRowCount(0);

    const QList<Macro> &macros = m_store.macros();
    m_table->setRowCount(int(macros.size()));
    for (int row = 0; row < macros.size(); ++row)
        fillRow(row, macros.at(row));

    m_populating = false;

    m_periodicMacro->clear();
    for (const Macro &macro : macros)
        m_periodicMacro->addItem(macro.payload);

    rebuildShortcuts();
    m_periodicButton->setEnabled(m_sendEnabled && !macros.isEmpty());
}

void MacrosPanel::commitTable()
{
    QList<Macro> &macros = m_store.macros();

    for (int row = 0; row < m_table->rowCount() && row < macros.size(); ++row) {
        const QTableWidgetItem *command = m_table->item(row, ColumnCommand);
        const QTableWidgetItem *shortcut = m_table->item(row, ColumnShortcut);
        if (command)
            macros[row].payload = command->text();
        if (shortcut)
            macros[row].shortcut = shortcut->text();
    }

    m_store.save();
    rebuildShortcuts();

    // Список для повторной отправки показывает команды и обязан следовать за правками.
    const int current = m_periodicMacro->currentIndex();
    const QSignalBlocker blocker(m_periodicMacro);
    m_periodicMacro->clear();
    for (const Macro &macro : macros)
        m_periodicMacro->addItem(macro.payload);

    // Проверка на пустоту обязательна: qBound(0, x, -1) — это min > max, и в отладочной
    // сборке Qt на этом останавливается.
    if (m_periodicMacro->count() > 0)
        m_periodicMacro->setCurrentIndex(qBound(0, current, m_periodicMacro->count() - 1));
}

void MacrosPanel::rebuildShortcuts()
{
    QList<PanelShortcut> shortcuts;

    const QList<Macro> &macros = m_store.macros();
    for (int i = 0; i < macros.size(); ++i) {
        const QString sequence = macros.at(i).shortcut;
        if (sequence.isEmpty())
            continue;

        shortcuts.append(PanelShortcut{
            .id = QStringLiteral("macro%1").arg(i),
            .title = tr("Macro: %1").arg(macros.at(i).payload),
            .defaultSequence = QKeySequence(sequence),
            // В разделе настроек не показываем: клавиша задаётся в самой таблице
            // макросов, и второе место правки неминуемо разошлось бы с первым.
            .userConfigurable = false,
        });
    }

    // Набор объявляется целиком — хост сам снимет прежние действия и создаст новые.
    host()->setShortcuts(shortcuts);
}

void MacrosPanel::sendMacro(int index)
{
    if (index < 0 || index >= m_store.macros().size())
        return;
    if (!m_sendEnabled) {
        host()->showStatusMessage(tr("The interface is not open."));
        return;
    }

    const Macro &macro = m_store.macros().at(index);

    QString error;
    const QByteArray data = macro.encode(&error);
    if (!error.isEmpty()) {
        host()->showStatusMessage(tr("Macro \"%1\": %2").arg(macro.payload, error));
        return;
    }
    if (data.isEmpty())
        return;

    host()->send(data);
}

void MacrosPanel::addMacro()
{
    Macro macro;
    // Первая свободная функциональная клавиша: подбирать её вручную каждому новому
    // макросу утомительно, а занятую назначить нельзя.
    macro.shortcut = m_store.suggestShortcut();
    m_store.macros().append(macro);
    m_store.save();

    reloadMacros();

    // Курсор сразу в новую ячейку команды: макрос без команды бесполезен, и следующее
    // действие пользователя очевидно.
    const int row = m_table->rowCount() - 1;
    m_table->setCurrentCell(row, ColumnCommand);
    m_table->editItem(m_table->item(row, ColumnCommand));
}

QList<int> MacrosPanel::selectedRows() const
{
    QList<int> rows;
    const QList<QModelIndex> indexes = m_table->selectionModel()->selectedRows();
    rows.reserve(indexes.size());
    for (const QModelIndex &index : indexes)
        rows.append(index.row());
    std::sort(rows.begin(), rows.end());
    return rows;
}

void MacrosPanel::removeSelected()
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty())
        return;

    if (rows.size() > 1) {
        // Одну строку удаляют не глядя и легко возвращают набором заново; десяток —
        // это уже потеря работы, и спросить дешевле, чем восстанавливать.
        const auto answer = QMessageBox::question(
            window(), tr("Delete macros"),
            tr("Delete %n selected macro(s)?", nullptr, int(rows.size())));
        if (answer != QMessageBox::Yes)
            return;
    }

    // С конца: удаление сверху сдвинуло бы номера всех строк ниже, и следующий индекс
    // указывал бы уже на чужой макрос.
    for (auto it = rows.crbegin(); it != rows.crend(); ++it) {
        if (*it >= 0 && *it < m_store.macros().size())
            m_store.macros().removeAt(*it);
    }
    m_store.save();
    reloadMacros();
}

void MacrosPanel::duplicateSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_store.macros().size())
        return;

    Macro copy = m_store.macros().at(row);
    // Сочетание не копируем: два макроса с одной клавишей — это неоднозначность, которую
    // пользователь не заказывал.
    copy.shortcut = m_store.suggestShortcut();

    m_store.macros().insert(row + 1, copy);
    m_store.save();
    reloadMacros();

    m_table->setCurrentCell(row + 1, ColumnCommand);
}

void MacrosPanel::importMacros()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import macros"), host()->dataDir(), tr("Macro files (*.json)"));
    if (path.isEmpty())
        return;

    int added = 0;
    if (!m_store.importFrom(path, &added)) {
        QMessageBox::warning(this, tr("Import macros"), tr("Could not read %1.").arg(path));
        return;
    }

    m_store.save();
    reloadMacros();
    host()->showStatusMessage(tr("%n macro(s) imported", nullptr, added));
}

void MacrosPanel::exportMacros()
{
    if (m_store.macros().isEmpty()) {
        host()->showStatusMessage(tr("There is nothing to export."));
        return;
    }

    // Выделенные, если выделено больше одного; иначе весь набор. Выгружать один
    // случайно подсвеченный макрос вместо набора никто не просит, а выделить десяток и
    // получить их отдельным файлом — просят.
    const QList<int> rows = selectedRows();
    const QList<int> selection = rows.size() > 1 ? rows : QList<int>{};

    const QString suggested =
        QStringLiteral("%1.json").arg(m_presetCombo->currentText());
    const QString path = QFileDialog::getSaveFileName(
        this, selection.isEmpty() ? tr("Export macros")
                                  : tr("Export %n selected macro(s)", nullptr,
                                       int(selection.size())),
        suggested, tr("Macro files (*.json)"));
    if (path.isEmpty())
        return;

    if (!m_store.exportTo(path, selection))
        QMessageBox::warning(this, tr("Export macros"), tr("Could not write %1.").arg(path));
    else
        host()->showStatusMessage(tr("Exported to %1").arg(path));
}

void MacrosPanel::showContextMenu(const QPoint &position)
{
    const QModelIndex index = m_table->indexAt(position);
    QMenu menu(this);

    if (!index.isValid()) {
        // Пустое место: здесь уместно только то, что создаёт макросы.
        menu.addAction(tr("New macro"), this, &MacrosPanel::addMacro);
        menu.addSeparator();
        menu.addAction(tr("Import macros..."), this, &MacrosPanel::importMacros);
        menu.addAction(tr("Export macros..."), this, &MacrosPanel::exportMacros);
        menu.exec(m_table->viewport()->mapToGlobal(position));
        return;
    }

    const int row = index.row();
    if (row >= m_store.macros().size())
        return;

    // Правый клик по уже выбранной строке открывает действия для всей группы. Обычный
    // setCurrentCell() очищает эту группу, поэтому меняем только current index.
    m_table->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    const Macro &macro = m_store.macros().at(row);

    menu.addAction(tr("Send now"), this, [this, row] { sendMacro(row); });

    // Положить в строку отправки, не отправляя: команду перед отправкой часто нужно
    // поправить — сменить адрес, номер регистра, — а править её в таблице значило бы
    // испортить сам макрос.
    menu.addAction(tr("Put into the send bar"), this, [this, row] {
        const Macro &macro = m_store.macros().at(row);
        host()->composeInSendBar(macro.payload, macro.format);
    });

    menu.addSeparator();
    menu.addAction(tr("Duplicate"), this, &MacrosPanel::duplicateSelected);
    menu.addAction(tr("Delete"), this, &MacrosPanel::removeSelected);
    menu.addSeparator();

    // Формат и терминация — взаимоисключающие значения, поэтому группа с отметкой, а не
    // выпадающий список в ячейке: список напротив выбранного читается быстрее.
    QMenu *formatMenu = menu.addMenu(tr("Format"));
    auto *formatGroup = new QActionGroup(&menu);
    formatGroup->setExclusive(true);
    for (const DataCodec::Format format : kFormats) {
        QAction *action = formatMenu->addAction(DataCodec::formatName(format));
        action->setCheckable(true);
        action->setChecked(macro.format == format);
        formatGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, row, format] {
            m_store.macros()[row].format = format;
            m_store.save();
            reloadMacros();
        });
    }

    QMenu *terminationMenu = menu.addMenu(tr("Termination"));
    auto *terminationGroup = new QActionGroup(&menu);
    terminationGroup->setExclusive(true);
    for (const DataCodec::Termination termination : kTerminations) {
        QAction *action = terminationMenu->addAction(DataCodec::terminationName(termination));
        action->setCheckable(true);
        action->setChecked(macro.termination == termination);
        terminationGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, row, termination] {
            m_store.macros()[row].termination = termination;
            m_store.save();
            reloadMacros();
        });
    }

    menu.exec(m_table->viewport()->mapToGlobal(position));
}

int MacrosPanel::selectedPeriodMs() const
{
    // Пункт списка несёт значение в данных, набранное руками — только в тексте. Читаем
    // сначала данные: у выбранного пункта текст переведён («1 с»), и разбирать его
    // числом значило бы зависеть от языка интерфейса.
    const QVariant data = m_periodInterval->currentData();
    if (data.isValid() && m_periodInterval->findText(m_periodInterval->currentText()) >= 0)
        return data.toInt();

    bool ok = false;
    const int typed = m_periodInterval->currentText().toInt(&ok);
    return ok ? qBound(kMinPeriodMs, typed, kMaxPeriodMs) : data.toInt();
}

void MacrosPanel::startPeriodic()
{
    if (m_periodicMacro->currentIndex() < 0 || !m_sendEnabled) {
        m_periodicButton->setChecked(false);
        return;
    }

    m_periodicStartedMs = QDateTime::currentMSecsSinceEpoch();
    m_periodicCount = 0;

    m_periodicTimer->start(selectedPeriodMs());
    updateIcons();
}

void MacrosPanel::stopPeriodic()
{
    m_periodicTimer->stop();
    m_actualLabel->clear();
    m_actualLabel->hide();
    updateIcons();
}

void MacrosPanel::updateActualInterval()
{
    const int requested = selectedPeriodMs();

    // Показываем фактический период только там, где он расходится с заданным: на
    // периодах от 10 мс таймер попадает точно, и лишняя цифра только отвлекала бы.
    if (requested >= 10 || m_periodicCount < 10) {
        m_actualLabel->hide();
        return;
    }

    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_periodicStartedMs;
    const double actual = double(elapsed) / m_periodicCount;

    m_actualLabel->setText(tr("actual: %1 ms").arg(actual, 0, 'f', 1));
    m_actualLabel->show();
}

void MacrosPanel::setSendEnabled(bool enabled)
{
    m_sendEnabled = enabled;
    m_periodicButton->setEnabled(enabled && !m_store.macros().isEmpty());

    if (!enabled && m_periodicButton->isChecked())
        m_periodicButton->setChecked(false);
}

} // namespace spotty
