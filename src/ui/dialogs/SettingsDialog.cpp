/**
 * \file SettingsDialog.cpp
 * \brief Реализация spotty::SettingsDialog.
 */
#include "SettingsDialog.h"

#include "../PanelPluginRegistry.h"

#include <spotty/api/IInterfacePlugin.h>
#include "../SchemaForm.h"

#include "InterfaceSettingsPanel.h"

#include <spotty/data/DataCodec.h>
#include <terminal/Packetizer.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QTreeWidget>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

/// \brief Размер квадратика цвета в палитре, px.
constexpr int kSwatchSize = 24;

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

/// \brief Подписи базовых цветов ANSI в порядке номеров 0..15.
const char *const kAnsiColorNames[16] = {
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Black"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Red"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Green"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Yellow"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Blue"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Magenta"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Cyan"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "White"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Bright black"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Bright red"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Bright green"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Bright yellow"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Bright blue"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Bright magenta"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Bright cyan"),
    QT_TRANSLATE_NOOP("spotty::SettingsDialog", "Bright white"),
};

/// \brief Закрасить кнопку выбранным цветом.
void paintSwatch(QToolButton *button, const QColor &color)
{
    button->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid "
                                         "palette(mid); border-radius: 3px; }")
                              .arg(color.name(QColor::HexRgb)));
    button->setProperty("spottyColor", color.name(QColor::HexRgb));
}

} // namespace

QList<ShortcutAction> SettingsDialog::builtinShortcutActions()
{
    // Идентификаторы попадают в файл настроек, поэтому не переводятся и не меняются.
    //
    // Поиска и записи журнала здесь больше нет: они принадлежат панелям, а те объявляют
    // свои сочетания сами. Держать их и тут значило бы иметь два места, где задаётся одно
    // и то же, — и первое же расхождение осталось бы незамеченным.
    return {
        {QStringLiteral("interface.toggle"), tr("Open / close interface"),
         QStringLiteral("Ctrl+O")},
        {QStringLiteral("terminal.clear"), tr("Clear terminal"), QStringLiteral("Ctrl+L")},
        {QStringLiteral("terminal.hex"), tr("Toggle hexadecimal dump"),
         QStringLiteral("Ctrl+H")},
        {QStringLiteral("terminal.timestamps"), tr("Toggle timestamps"),
         QStringLiteral("Ctrl+T")},
        {QStringLiteral("send.focus"), tr("Focus send bar"), QStringLiteral("Ctrl+E")},
    };
}

SettingsDialog::SettingsDialog(const AppSettings &settings, const QStringList &ansiPalette,
                               InterfaceRegistry *registry, PluginManager *plugins,
                               PanelPluginRegistry *panels,
                               const QHash<QString, QVariantMap> &pluginValues,
                               const QList<ShortcutAction> &shortcutActions, QWidget *parent)
    : QDialog(parent)
    , m_initial(settings)
    , m_pluginValues(pluginValues)
    , m_shortcutActions(shortcutActions.isEmpty() ? builtinShortcutActions() : shortcutActions)
{
    setWindowTitle(tr("Settings"));
    setModal(true);
    resize(720, 560);

    m_categories = new QListWidget(this);
    // Список разделов оформляется отдельно от прочих списков (правило #settingsCategories
    // в QSS): это навигация, а не редактируемые данные, и выглядеть она должна как
    // боковое меню, а не как таблица.
    m_categories->setObjectName(QStringLiteral("settingsCategories"));
    m_categories->setMaximumWidth(180);
    // Названия разделов не переносятся, и самое длинное из них при переводе легко
    // перерастает ширину списка. Полоса прокрутки под ним ничего не даёт — прокручивать
    // приходится, чтобы дочитать название, — а место в списке занимает.
    m_categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categories->setTextElideMode(Qt::ElideRight);
    // Список разделов больше не литерал: часть их приносят плагины, и число разделов
    // становится известно только здесь.
    QStringList titles{tr("General"), tr("Terminal"), tr("Send"), tr("Data")};

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildGeneralPage());
    m_pages->addWidget(buildTerminalPage());
    m_pages->addWidget(buildSendPage());
    m_pages->addWidget(buildDataPage());

    // Разделы плагинов идут между настройками программы и «Интерфейсами»: это тоже
    // настройки того, что показывает окно, а не устройств.
    buildPluginPages(panels, titles);

    titles << tr("Interfaces") << tr("Plugins") << tr("Shortcuts");
    m_pages->addWidget(buildInterfacesPage(registry, plugins));
    m_pages->addWidget(buildPluginsPage(plugins, panels));
    m_pages->addWidget(buildShortcutsPage());

    m_categories->addItems(titles);

    connect(m_categories, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);
    m_categories->setCurrentRow(0);

    // Палитру заполняем действующими цветами: пользователь должен видеть, от чего
    // отталкивается, даже если своих цветов ещё не задавал.
    const QStringList palette = m_initial.ansiPalette.size() == 16 ? m_initial.ansiPalette
                                                                  : ansiPalette;
    for (int i = 0; i < m_paletteButtons.size() && i < palette.size(); ++i)
        paintSwatch(m_paletteButtons.at(i), QColor(palette.at(i)));
    m_paletteCustomized = m_initial.ansiPalette.size() == 16;

    auto *content = new QHBoxLayout;
    content->addWidget(m_categories);
    content->addWidget(m_pages, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(content, 1);
    layout->addWidget(buttons);

    updateEnabledState();
}

QWidget *SettingsDialog::buildGeneralPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;

    m_language = new QComboBox(page);
    m_language->addItem(tr("System"), QStringLiteral("system"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_language->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    m_language->setCurrentIndex(qMax(0, m_language->findData(m_initial.language)));
    form->addRow(tr("Language"), m_language);

    auto *languageHint = new QLabel(tr("Takes effect after restarting Spotty."), page);
    languageHint->setObjectName(QStringLiteral("hintLabel"));
    form->addRow(QString(), languageHint);

    m_theme = new QComboBox(page);
    m_theme->addItem(tr("Dark"), QStringLiteral("dark"));
    m_theme->addItem(tr("Light"), QStringLiteral("light"));
    m_theme->setCurrentIndex(qMax(0, m_theme->findData(m_initial.theme)));
    form->addRow(tr("Theme"), m_theme);

    layout->addLayout(form);

    m_autoOpen = new QCheckBox(tr("Open the remembered interface on startup"), page);
    m_autoOpen->setChecked(m_initial.autoOpenLastInterface);
    m_autoOpen->setToolTip(tr("Opening a port asserts DTR, which resets many boards, and "
                              "takes the port away from any other program using it."));
    layout->addWidget(m_autoOpen);

    m_singleInstance = new QCheckBox(tr("Only one running copy of Spotty"), page);
    m_singleInstance->setChecked(m_initial.singleInstance);
    m_singleInstance->setToolTip(tr("Starting Spotty again raises the existing window "
                                    "instead of opening a second one."));
    layout->addWidget(m_singleInstance);

    layout->addStretch(1);

    // Внизу страницы и в собственной рамке: действие необратимо и не должно попасться под
    // руку рядом с обычными переключателями.
    auto *resetBox = new QGroupBox(tr("Reset"), page);
    auto *resetLayout = new QVBoxLayout(resetBox);

    auto *resetHint = new QLabel(
        tr("Erases all settings, remembered interfaces and the send history. Cannot be undone."),
        resetBox);
    resetHint->setObjectName(QStringLiteral("hintLabel"));
    resetHint->setWordWrap(true);
    resetLayout->addWidget(resetHint);

    auto *resetButton = new QPushButton(tr("Reset everything to defaults…"), resetBox);
    connect(resetButton, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::warning(
            this, tr("Reset to defaults"),
            tr("This erases all settings, remembered interfaces and the send history, and "
               "cannot be undone.\n\nContinue?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;

        // Сброс применяется немедленно, в обход обычной модели «правим копию, отдаём по
        // Ok»: интерфейсы и история не принадлежат диалогу, откатить их из Cancel всё равно
        // нечем, а после подтверждения в этом окне уже нечего редактировать.
        Q_EMIT resetToDefaultsRequested();
        reject();
    });
    resetLayout->addWidget(resetButton);

    layout->addWidget(resetBox);
    return page;
}

QWidget *SettingsDialog::buildTerminalPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;

    m_fontFamily = new QFontComboBox(page);
    // Только моноширинные: пропорциональный шрифт ломает и колонки HEX-дампа, и
    // выравнивание вывода устройства.
    m_fontFamily->setFontFilters(QFontComboBox::MonospacedFonts);
    if (!m_initial.fontFamily.isEmpty())
        m_fontFamily->setCurrentFont(QFont(m_initial.fontFamily));
    form->addRow(tr("Font"), m_fontFamily);

    m_fontSize = new QSpinBox(page);
    m_fontSize->setRange(6, 32);
    m_fontSize->setValue(m_initial.fontSize > 0 ? m_initial.fontSize : 12);
    form->addRow(tr("Font size"), m_fontSize);

    m_maxLines = new QSpinBox(page);
    m_maxLines->setRange(100, 1'000'000);
    m_maxLines->setSingleStep(1000);
    m_maxLines->setValue(m_initial.maxLines);
    m_maxLines->setSuffix(tr(" lines"));
    form->addRow(tr("Buffer size"), m_maxLines);

    m_encoding = new QComboBox(page);
    m_encoding->addItem(QStringLiteral("UTF-8"), QStringLiteral("utf-8"));
    m_encoding->addItem(QStringLiteral("Latin-1"), QStringLiteral("latin1"));
    m_encoding->setCurrentIndex(qMax(0, m_encoding->findData(m_initial.encoding)));
    form->addRow(tr("Received text encoding"), m_encoding);

    m_hexBytesPerRow = new QSpinBox(page);
    m_hexBytesPerRow->setRange(1, 64);
    m_hexBytesPerRow->setValue(m_initial.hexBytesPerRow);
    form->addRow(tr("Bytes per hex row"), m_hexBytesPerRow);

    m_timestampFormat = new QLineEdit(m_initial.timestampFormat, page);
    m_timestampFormat->setToolTip(tr("Qt date/time format, for example HH:mm:ss.zzz"));
    form->addRow(tr("Timestamp format"), m_timestampFormat);

    layout->addLayout(form);

    m_showTimestamps = new QCheckBox(tr("Show timestamps"), page);
    m_showTimestamps->setChecked(m_initial.showTimestamps);

    m_relativeTimestamps = new QCheckBox(tr("Show time relative to the previous line"), page);
    m_relativeTimestamps->setChecked(m_initial.relativeTimestamps);

    m_showDirection = new QCheckBox(tr("Show transmit and receive marks"), page);
    m_showDirection->setChecked(m_initial.showDirection);

    m_localEcho = new QCheckBox(tr("Echo sent data into the terminal"), page);
    m_localEcho->setChecked(m_initial.localEcho);

    layout->addWidget(m_showTimestamps);
    layout->addWidget(m_relativeTimestamps);
    layout->addWidget(m_showDirection);
    layout->addWidget(m_localEcho);

    connect(m_showTimestamps, &QCheckBox::toggled, this, &SettingsDialog::updateEnabledState);
    connect(m_relativeTimestamps, &QCheckBox::toggled, this,
            &SettingsDialog::updateEnabledState);

    // --- Палитра ANSI ----------------------------------------------------------------

    auto *paletteBox = new QGroupBox(tr("ANSI colours"), page);
    auto *paletteLayout = new QVBoxLayout(paletteBox);

    auto *swatchRow = new QHBoxLayout;
    swatchRow->setSpacing(3);
    for (int i = 0; i < 16; ++i) {
        auto *button = new QToolButton(paletteBox);
        button->setFixedSize(kSwatchSize, kSwatchSize);
        button->setToolTip(tr(kAnsiColorNames[i]));
        connect(button, &QToolButton::clicked, this, [this, button] {
            const QColor current(button->property("spottyColor").toString());
            const QColor chosen = QColorDialog::getColor(current, this, tr("ANSI colour"));
            if (!chosen.isValid())
                return;
            paintSwatch(button, chosen);
            m_paletteCustomized = true;
        });
        m_paletteButtons.append(button);
        swatchRow->addWidget(button);
        if (i == 7)
            swatchRow->addSpacing(8);
    }
    swatchRow->addStretch(1);
    paletteLayout->addLayout(swatchRow);

    m_resetPalette = new QToolButton(paletteBox);
    m_resetPalette->setText(tr("Reset to theme colours"));
    m_resetPalette->setToolTip(tr("Colours will follow the theme again."));
    connect(m_resetPalette, &QToolButton::clicked, this, [this] {
        m_paletteCustomized = false;
        // Кнопки не перекрашиваем: они показывают, что было, а сброс проявится после
        // подтверждения. Иначе пришлось бы держать здесь копию цветов темы.
        m_resetPalette->setEnabled(false);
    });
    paletteLayout->addWidget(m_resetPalette, 0, Qt::AlignLeft);

    layout->addWidget(paletteBox);
    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildSendPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;

    m_sendFormat = new QComboBox(page);
    for (const DataCodec::Format format : kFormats)
        m_sendFormat->addItem(DataCodec::formatName(format), int(format));
    m_sendFormat->setCurrentIndex(qMax(0, m_sendFormat->findData(m_initial.sendFormat)));
    form->addRow(tr("Default format"), m_sendFormat);

    m_sendTermination = new QComboBox(page);
    for (const DataCodec::Termination termination : kTerminations)
        m_sendTermination->addItem(DataCodec::terminationName(termination), int(termination));
    m_sendTermination->setCurrentIndex(
        qMax(0, m_sendTermination->findData(m_initial.sendTermination)));
    form->addRow(tr("Default termination"), m_sendTermination);

    m_historySize = new QSpinBox(page);
    m_historySize->setRange(10, 10000);
    m_historySize->setValue(m_initial.historySize);
    m_historySize->setSuffix(tr(" entries"));
    form->addRow(tr("History size"), m_historySize);

    layout->addLayout(form);

    auto *hint = new QLabel(tr("History is kept in a plain text file next to the "
                               "configuration and survives restarts."),
                            page);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    layout->addStretch(1);
    return page;
}

void SettingsDialog::buildPluginPages(PanelPluginRegistry *panels, QStringList &titles)
{
    if (!panels)
        return;

    for (IPanelPlugin *plugin : panels->plugins()) {
        const SettingsSchema schema = plugin->settingsSchema();
        if (schema.isEmpty())
            continue;

        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);

        auto *form = new SchemaForm(schema, m_pluginValues.value(plugin->pluginId()), page);
        layout->addWidget(form);
        layout->addStretch(1);

        m_pluginForms.insert(plugin->pluginId(), form);
        m_pages->addWidget(page);
        titles << plugin->displayName();
    }
}

QHash<QString, QVariantMap> SettingsDialog::pluginSettings() const
{
    QHash<QString, QVariantMap> result;
    for (auto it = m_pluginForms.cbegin(); it != m_pluginForms.cend(); ++it)
        result.insert(it.key(), it.value()->values());
    return result;
}

QWidget *SettingsDialog::buildDataPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;

    m_packetizerMode = new QComboBox(page);
    m_packetizerMode->addItem(tr("Stream (split on line breaks)"),
                              int(Packetizer::Mode::Stream));
    m_packetizerMode->addItem(tr("Inter-byte timeout"),
                              int(Packetizer::Mode::InterByteTimeout));
    m_packetizerMode->addItem(tr("Delimiter"), int(Packetizer::Mode::Delimiter));
    m_packetizerMode->addItem(tr("Fixed length"), int(Packetizer::Mode::FixedLength));
    m_packetizerMode->setCurrentIndex(
        qMax(0, m_packetizerMode->findData(m_initial.packetizerMode)));
    form->addRow(tr("Split incoming data by"), m_packetizerMode);

    m_packetizerTimeout = new QSpinBox(page);
    m_packetizerTimeout->setRange(1, 10000);
    m_packetizerTimeout->setValue(m_initial.packetizerTimeoutMs);
    m_packetizerTimeout->setSuffix(tr(" ms"));
    form->addRow(tr("Gap"), m_packetizerTimeout);

    m_packetizerDelimiter = new QLineEdit(m_initial.packetizerDelimiterHex, page);
    m_packetizerDelimiter->setToolTip(tr("Hexadecimal, for example 0A or 0D0A"));
    form->addRow(tr("Delimiter"), m_packetizerDelimiter);

    m_packetizerLength = new QSpinBox(page);
    m_packetizerLength->setRange(1, 65536);
    m_packetizerLength->setValue(m_initial.packetizerFixedLength);
    m_packetizerLength->setSuffix(tr(" bytes"));
    form->addRow(tr("Message length"), m_packetizerLength);

    layout->addLayout(form);

    auto *hint = new QLabel(
        tr("A binary stream without line breaks collapses into one endless line. "
           "Splitting by an inter-byte gap is how most binary protocols over UART "
           "frame their messages."),
        page);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(m_packetizerMode, &QComboBox::currentIndexChanged, this,
            &SettingsDialog::updateEnabledState);

    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildInterfacesPage(InterfaceRegistry *registry, PluginManager *plugins)
{
    // Тот же widget, что открывает кнопка настроек рядом с выбором интерфейса — раздел
    // здесь не переизобретает его, а лишь даёт второй вход в то же самое.
    m_interfacePanel = new InterfaceSettingsPanel(registry, plugins, this);
    return m_interfacePanel;
}

QWidget *SettingsDialog::buildPluginsPage(PluginManager *plugins, PanelPluginRegistry *panels)
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *tree = new QTreeWidget(page);
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("Plugin"), tr("Details")});
    tree->setRootIsDecorated(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    const auto addGroup = [tree](const QString &title) {
        auto *group = new QTreeWidgetItem(tree, {title, QString()});
        group->setExpanded(true);
        group->setFirstColumnSpanned(true);
        return group;
    };

    QTreeWidgetItem *interfaces = addGroup(tr("Interfaces"));
    if (plugins) {
        for (IInterfacePlugin *plugin : plugins->plugins())
            new QTreeWidgetItem(interfaces, {plugin->pluginId(), plugin->displayName()});
    }

    QTreeWidgetItem *panelGroup = addGroup(tr("Panels and data"));
    if (panels) {
        for (IPanelPlugin *plugin : panels->plugins()) {
            auto *item = new QTreeWidgetItem(panelGroup,
                                             {plugin->pluginId(), plugin->displayName()});
            for (const PanelPluginRegistry::PanelEntry &entry : panels->panels()) {
                if (entry.plugin == plugin)
                    new QTreeWidgetItem(item, {entry.descriptor.id, entry.descriptor.title});
            }
            item->setExpanded(true);
        }
    }

    // Отвергнутые собираются из обоих реестров: у каждого свои причины отказа, и
    // пользователю всё равно, кто именно не принял файл.
    QList<PluginManager::LoadFailure> failures;
    if (plugins)
        failures += plugins->failures();
    if (panels)
        failures += panels->failures();

    if (!failures.isEmpty()) {
        QTreeWidgetItem *rejected = addGroup(tr("Rejected"));
        for (const PluginManager::LoadFailure &failure : failures) {
            auto *item = new QTreeWidgetItem(rejected, {failure.path, failure.reason});
            item->setToolTip(0, failure.path);
            item->setToolTip(1, failure.reason);
        }
    }

    layout->addWidget(tree, 1);

    auto *dirsTitle = new QLabel(tr("Searched directories"), page);
    dirsTitle->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(dirsTitle);

    auto *dirs = new QLabel(page);
    dirs->setObjectName(QStringLiteral("hintLabel"));
    dirs->setWordWrap(true);
    dirs->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const QStringList searched = plugins ? plugins->searchedDirectories() : QStringList{};
    dirs->setText(searched.isEmpty() ? tr("None — plugins are built into the executable.")
                                     : searched.join(u'\n'));
    layout->addWidget(dirs);

    return page;
}

QWidget *SettingsDialog::buildShortcutsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;

    for (const ShortcutAction &action : m_shortcutActions) {
        auto *editor = new QKeySequenceEdit(page);
        const QString stored = m_initial.shortcuts.value(action.id, action.defaultSequence);
        editor->setKeySequence(QKeySequence(stored));
        form->addRow(action.title, editor);
        m_shortcutEditors.insert(action.id, editor);
    }

    layout->addLayout(form);

    auto *hint = new QLabel(tr("Macro shortcuts are assigned in the Macros panel, on each "
                               "macro."),
                            page);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    layout->addStretch(1);
    return page;
}

void SettingsDialog::updateEnabledState()
{
    // Формат метки времени не нужен ни при выключенных метках, ни в относительном режиме:
    // там формат фиксирован.
    m_timestampFormat->setEnabled(m_showTimestamps->isChecked()
                                  && !m_relativeTimestamps->isChecked());
    m_relativeTimestamps->setEnabled(m_showTimestamps->isChecked());

    const auto mode = Packetizer::Mode(m_packetizerMode->currentData().toInt());
    m_packetizerTimeout->setEnabled(mode == Packetizer::Mode::InterByteTimeout);
    m_packetizerDelimiter->setEnabled(mode == Packetizer::Mode::Delimiter);
    m_packetizerLength->setEnabled(mode == Packetizer::Mode::FixedLength);
}

AppSettings SettingsDialog::settings() const
{
    AppSettings result = m_initial;

    result.language = m_language->currentData().toString();
    result.theme = m_theme->currentData().toString();
    result.autoOpenLastInterface = m_autoOpen->isChecked();
    result.singleInstance = m_singleInstance->isChecked();

    result.fontFamily = m_fontFamily->currentFont().family();
    result.fontSize = m_fontSize->value();
    result.maxLines = m_maxLines->value();
    result.showTimestamps = m_showTimestamps->isChecked();
    result.relativeTimestamps = m_relativeTimestamps->isChecked();
    result.timestampFormat = m_timestampFormat->text();
    result.showDirection = m_showDirection->isChecked();
    result.localEcho = m_localEcho->isChecked();
    result.hexBytesPerRow = m_hexBytesPerRow->value();
    result.encoding = m_encoding->currentData().toString();

    if (m_paletteCustomized) {
        QStringList palette;
        palette.reserve(m_paletteButtons.size());
        for (QToolButton *button : m_paletteButtons)
            palette.append(button->property("spottyColor").toString());
        result.ansiPalette = palette;
    } else {
        // Пустой список означает «следовать теме», а не «чёрная палитра».
        result.ansiPalette.clear();
    }

    result.sendFormat = m_sendFormat->currentData().toInt();
    result.sendTermination = m_sendTermination->currentData().toInt();
    result.historySize = m_historySize->value();


    result.packetizerMode = m_packetizerMode->currentData().toInt();
    result.packetizerTimeoutMs = m_packetizerTimeout->value();
    result.packetizerDelimiterHex = m_packetizerDelimiter->text();
    result.packetizerFixedLength = m_packetizerLength->value();

    result.shortcuts.clear();
    for (const ShortcutAction &action : m_shortcutActions) {
        QKeySequenceEdit *editor = m_shortcutEditors.value(action.id);
        if (!editor)
            continue;
        const QString sequence = editor->keySequence().toString(QKeySequence::PortableText);
        // Совпадающее с умолчанием не сохраняем: иначе смена умолчания в новой версии
        // не дошла бы до тех, кто ничего не менял.
        if (sequence != action.defaultSequence)
            result.shortcuts.insert(action.id, sequence);
    }

    return result;
}

bool SettingsDialog::requiresRestart() const
{
    const AppSettings updated = settings();
    return updated.language != m_initial.language
        || updated.singleInstance != m_initial.singleInstance;
}

} // namespace spotty
