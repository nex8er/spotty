/**
 * \file MainWindow.cpp
 * \brief Реализация spotty::MainWindow.
 */
#include "MainWindow.h"

#include "InterfaceBar.h"
#include "theme/MdiIcons.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <settings/Paths.h>
#include <settings/SettingsStore.h>
#include <spotty/api/IInterfacePlugin.h>

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr int kPanelGlyphSize = 20; ///< Размер значка на рейке панелей, px.

// Ключи настроек. Собраны здесь, чтобы опечатка в строке не разошлась между записью и
// чтением: такую ошибку компилятор не поймает, а проявится она лишь как «настройка не
// сохраняется».
constexpr auto kKeyGeometry = "window/geometry";
constexpr auto kKeySplitter = "window/splitter";
constexpr auto kKeyPanelIndex = "window/panelIndex";
constexpr auto kKeyTheme = "appearance/theme";
constexpr auto kKeyInterface = "session/interfaceId";

/// \brief Описание одной правой панели.
struct PanelSpec
{
    char32_t glyph;    ///< Значок на рейке.
    const char *title; ///< Заголовок, помеченный для перевода.
};

/**
 * \brief Правые панели в порядке, заданном планом.
 *
 * Пока каждая — заглушка; этап 3 заменяет страницы за этими кнопками.
 */
const PanelSpec kPanels[] = {
    {mdi::Flash, QT_TRANSLATE_NOOP("spotty::MainWindow", "Macros")},
    {mdi::RecordCircleOutline, QT_TRANSLATE_NOOP("spotty::MainWindow", "Logging")},
    {mdi::Magnify, QT_TRANSLATE_NOOP("spotty::MainWindow", "Search")},
    {mdi::ShuffleVariant, QT_TRANSLATE_NOOP("spotty::MainWindow", "Generator")},
};

} // namespace

MainWindow::MainWindow(const AppContext &context, QWidget *parent)
    : QMainWindow(parent)
    , m_context(context)
{
    setWindowTitle(QStringLiteral("Spotty %1").arg(QLatin1String(SPOTTY_VERSION)));

    buildUi();
    buildMenus();
    restoreState();
    showStartupReport();

    if (m_context.theme) {
        connect(m_context.theme, &ThemeManager::themeChanged, this, [this] { updateIcons(); });
    }
    updateIcons();
}

void MainWindow::buildUi()
{
    m_interfaceBar = new InterfaceBar(m_context, this);
    connect(m_interfaceBar, &InterfaceBar::interfaceSelected, this, [this](const QString &id) {
        // Запоминание выбора — обещание этапа 1; открытие канала появится на этапе 2.
        m_context.settings->setValue(QLatin1String(kKeyInterface), id);
    });

    m_terminalPlaceholder = new QPlainTextEdit(this);
    m_terminalPlaceholder->setReadOnly(true);
    m_terminalPlaceholder->setFrameShape(QFrame::NoFrame);
    QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monospace.setPointSize(monospace.pointSize() + 1);
    m_terminalPlaceholder->setFont(monospace);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_terminalPlaceholder);
    m_splitter->addWidget(buildSidePanel());
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    // Панель должна полностью схлопываться: при работе с длинными строками терминалу нужна
    // вся ширина окна.
    m_splitter->setChildrenCollapsible(true);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_interfaceBar);
    layout->addWidget(m_splitter, 1);
    layout->addWidget(buildSendBar());

    setCentralWidget(central);
    statusBar()->showMessage(tr("Ready"));
    resize(1180, 720);
}

QWidget *MainWindow::buildSidePanel()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("sidePanel"));
    panel->setMinimumWidth(260);

    // Вертикальная рейка значков вместо полосы вкладок: она читается при любой ширине
    // панели и не вносит в оформление лишних рамок.
    auto *rail = new QWidget(panel);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(4, 6, 4, 6);
    railLayout->setSpacing(4);

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);

    m_panelStack = new QStackedWidget(panel);

    int index = 0;
    for (const PanelSpec &spec : kPanels) {
        const QString title = tr(spec.title);

        auto *button = new QToolButton(rail);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolTip(title);
        button->setIconSize(QSize(kPanelGlyphSize, kPanelGlyphSize));
        group->addButton(button, index);
        railLayout->addWidget(button);
        m_panelButtons.append(button);

        auto *page = new QWidget(m_panelStack);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(10, 10, 10, 10);

        auto *heading = new QLabel(title, page);
        heading->setObjectName(QStringLiteral("panelTitle"));

        auto *hint = new QLabel(tr("Not implemented yet."), page);
        hint->setObjectName(QStringLiteral("hintLabel"));
        hint->setWordWrap(true);

        pageLayout->addWidget(heading);
        pageLayout->addWidget(hint);
        pageLayout->addStretch(1);

        m_panelStack->addWidget(page);
        ++index;
    }

    railLayout->addStretch(1);

    connect(group, &QButtonGroup::idClicked, m_panelStack, &QStackedWidget::setCurrentIndex);

    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(rail);
    layout->addWidget(m_panelStack, 1);

    if (!m_panelButtons.isEmpty())
        m_panelButtons.first()->setChecked(true);

    return panel;
}

QWidget *MainWindow::buildSendBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("sendBar"));

    // Всё выключено: раскладка задаёт форму, наполнение приходит на этапе 2 вместе с
    // историей, автодополнением по Tab и кодированием форматов.
    auto *input = new QLineEdit(bar);
    input->setPlaceholderText(tr("Data to send"));
    input->setEnabled(false);

    auto *format = new QComboBox(bar);
    format->addItems({tr("Text"), tr("Hex"), tr("Base64")});
    format->setEnabled(false);

    auto *termination = new QComboBox(bar);
    termination->addItems({QStringLiteral("None"), QStringLiteral("LF"), QStringLiteral("CR"),
                           QStringLiteral("CR+LF"), QStringLiteral("NUL")});
    termination->setCurrentIndex(3);
    termination->setEnabled(false);

    auto *send = new QPushButton(tr("Send"), bar);
    send->setEnabled(false);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);
    layout->addWidget(input, 1);
    layout->addWidget(format);
    layout->addWidget(termination);
    layout->addWidget(send);

    return bar;
}

void MainWindow::buildMenus()
{
    // На macOS пункты «Settings» и «Quit» переезжают в меню приложения, а «About» — тоже
    // туда, поэтому меню «File» и «Help» в строке меню не показываются. Это штатное
    // поведение Qt, а не потерянные пункты.
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Settings..."))->setEnabled(false);
    fileMenu->addSeparator();
    QAction *quit = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));

    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);

    const auto addTheme = [&](const QString &title, ThemeManager::Theme theme) {
        QAction *action = themeMenu->addAction(title);
        action->setCheckable(true);
        action->setChecked(m_context.theme && m_context.theme->theme() == theme);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, theme] { setTheme(theme); });
    };
    addTheme(tr("&Dark"), ThemeManager::Theme::Dark);
    addTheme(tr("&Light"), ThemeManager::Theme::Light);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Spotty"), this, [this] {
        statusBar()->showMessage(
            tr("Spotty %1 - modular terminal port monitor").arg(QLatin1String(SPOTTY_VERSION)),
            5000);
    });
}

void MainWindow::setTheme(ThemeManager::Theme theme)
{
    if (!m_context.theme)
        return;

    m_context.theme->setTheme(theme);
    m_context.settings->setValue(QLatin1String(kKeyTheme), ThemeManager::themeToString(theme));
}

void MainWindow::updateIcons()
{
    for (int i = 0; i < m_panelButtons.size() && i < int(std::size(kPanels)); ++i)
        m_panelButtons.at(i)->setIcon(MdiIcons::icon(kPanels[i].glyph, kPanelGlyphSize));
}

void MainWindow::showStartupReport()
{
    QStringList lines;
    lines << tr("Spotty %1").arg(QLatin1String(SPOTTY_VERSION));
    lines << tr("Configuration: %1%2")
                 .arg(Paths::configDir(), Paths::isPortable() ? tr("  (portable)") : QString());
    lines << QString();

    const QList<IInterfacePlugin *> plugins = m_context.plugins->plugins();
    lines << tr("Plugins loaded: %1").arg(plugins.size());
    for (IInterfacePlugin *plugin : plugins) {
        lines << QStringLiteral("  %1  (%2)  -  %3 %4")
                     .arg(plugin->displayName(), plugin->pluginId())
                     .arg(plugin->enumerate().size())
                     .arg(tr("interface(s)"));
    }

    // Отклонённые плагины показываются с причиной: чаще всего это несовпадение версии Qt
    // или компилятора, и без объяснения плагин выглядел бы просто исчезнувшим.
    const QList<PluginManager::LoadFailure> failures = m_context.plugins->failures();
    if (!failures.isEmpty()) {
        lines << QString();
        lines << tr("Rejected: %1").arg(failures.size());
        for (const PluginManager::LoadFailure &failure : failures)
            lines << QStringLiteral("  %1\n    %2").arg(failure.path, failure.reason);
    }

    lines << QString();
    lines << tr("Searched for plugins in:");
    const QStringList dirs = m_context.plugins->searchedDirectories();
    if (dirs.isEmpty())
        lines << tr("  (no existing directories)");
    for (const QString &dir : dirs)
        lines << QStringLiteral("  %1").arg(dir);

    m_terminalPlaceholder->setPlainText(lines.join(u'\n'));
}

void MainWindow::restoreState()
{
    const QByteArray geometry =
        m_context.settings->value(QLatin1String(kKeyGeometry)).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    const QByteArray splitter =
        m_context.settings->value(QLatin1String(kKeySplitter)).toByteArray();
    if (!splitter.isEmpty())
        m_splitter->restoreState(splitter);

    const int panelIndex = m_context.settings->value(QLatin1String(kKeyPanelIndex), 0).toInt();
    if (panelIndex >= 0 && panelIndex < m_panelButtons.size()) {
        m_panelButtons.at(panelIndex)->setChecked(true);
        m_panelStack->setCurrentIndex(panelIndex);
    }

    m_interfaceBar->setCurrentInterfaceId(
        m_context.settings->value(QLatin1String(kKeyInterface)).toString());
}

void MainWindow::persistState()
{
    m_context.settings->setValue(QLatin1String(kKeyGeometry), saveGeometry());
    m_context.settings->setValue(QLatin1String(kKeySplitter), m_splitter->saveState());
    m_context.settings->setValue(QLatin1String(kKeyPanelIndex), m_panelStack->currentIndex());
    // Явный save(), а не отложенный: приложение вот-вот завершится, ждать таймер некому.
    m_context.settings->save();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    persistState();
    QMainWindow::closeEvent(event);
}

} // namespace spotty
