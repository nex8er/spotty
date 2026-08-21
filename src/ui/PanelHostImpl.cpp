/**
 * \file PanelHostImpl.cpp
 * \brief Реализация spotty::PanelHostImpl.
 */
#include "PanelHostImpl.h"

#include "MainWindow.h"
#include "TerminalView.h"
#include "theme/MdiIcons.h"
#include "theme/ThemeManager.h"

#include <InterfaceRegistry.h>
#include <Session.h>
#include <settings/Paths.h>
#include <settings/SettingsStore.h>

#include <spotty/data/FileUtils.h>

#include <QAction>
#include <QDir>

namespace spotty {

namespace {

/// \brief Корень настроек всех панельных плагинов в `settings.json`.
constexpr auto kSettingsRoot = "plugins";

} // namespace

PanelHostImpl::PanelHostImpl(const AppContext &context, MainWindow *window, QString pluginId)
    : IPanelHost(window)
    , m_context(context)
    , m_window(window)
    , m_pluginId(std::move(pluginId))
{
    if (Session *session = m_context.session) {
        // Сигналы сессии пробрасываются как есть: хост здесь только сужает видимость, а
        // не меняет смысл.
        connect(session, &Session::dataLogged, this, &IPanelHost::dataLogged);
        connect(session, &Session::dataReceived, this, &IPanelHost::dataReceived);
        connect(session->buffer(), &TerminalBuffer::linesAppended, this,
                &IPanelHost::terminalLinesAppended);
        connect(session->buffer(), &TerminalBuffer::lineFinalized, this,
                &IPanelHost::terminalLineFinalized);
    }

    if (TerminalView *terminal = m_window ? m_window->terminalView() : nullptr) {
        connect(terminal, &TerminalView::matchCountChanged, this,
                &IPanelHost::matchCountChanged);
    }
}

PanelHostImpl::~PanelHostImpl() = default;

// --- Отправка ------------------------------------------------------------------------

void PanelHostImpl::send(const QByteArray &data, SendTarget target)
{
    if (m_window)
        m_window->sendToTarget(data, target);
}

DataCodec::Termination PanelHostImpl::sendTermination() const
{
    return m_window ? m_window->sendTermination() : DataCodec::Termination::None;
}

void PanelHostImpl::composeInSendBar(const QString &text, DataCodec::Format format)
{
    if (m_window)
        m_window->composeInSendBar(text, format);
}

bool PanelHostImpl::dualTransportEnabled() const
{
    return m_window && m_window->dualTransportEnabled();
}

bool PanelHostImpl::secondInterfaceAvailable() const
{
    return m_window && m_window->secondInterfaceAvailable();
}

// --- Состояние интерфейса --------------------------------------------------------------

ChannelState PanelHostImpl::channelState() const
{
    return m_context.session ? m_context.session->state() : ChannelState::Closed;
}

QString PanelHostImpl::interfaceId() const
{
    return m_context.session ? m_context.session->interfaceId() : QString();
}

QString PanelHostImpl::interfaceName() const
{
    if (!m_context.registry)
        return {};
    const InterfaceEntry *entry = m_context.registry->entry(interfaceId());
    return entry ? entry->descriptor.systemName : QString();
}

QString PanelHostImpl::interfaceAlias() const
{
    if (!m_context.registry)
        return {};
    const InterfaceEntry *entry = m_context.registry->entry(interfaceId());
    // displayName() уже умеет «псевдоним, а если пусто — системное имя»; повторять эту
    // развилку здесь значило бы завести второе место, где она может разойтись.
    return entry ? entry->displayName() : QString();
}

// --- Свои настройки и свои файлы ---------------------------------------------------------

QString PanelHostImpl::settingsKey(const QString &key) const
{
    return QStringLiteral("%1/%2/%3").arg(QLatin1String(kSettingsRoot), m_pluginId, key);
}

QVariant PanelHostImpl::value(const QString &key, const QVariant &fallback) const
{
    return m_context.settings ? m_context.settings->value(settingsKey(key), fallback)
                              : fallback;
}

void PanelHostImpl::setValue(const QString &key, const QVariant &value)
{
    if (m_context.settings)
        m_context.settings->setValue(settingsKey(key), value);
}

QString PanelHostImpl::dataDir() const
{
    const QString path = QDir(Paths::configDir()).filePath(m_pluginId);
    ensureDir(path);
    return path;
}

QString PanelHostImpl::documentsDir() const
{
    return Paths::defaultLogDir();
}

// --- Общение с пользователем ---------------------------------------------------------------

void PanelHostImpl::showStatusMessage(const QString &message)
{
    if (m_window)
        m_window->showStatusMessage(message);
}

void PanelHostImpl::activatePanel(const QString &panelId)
{
    if (m_window)
        m_window->activatePanel(panelId);
}

void PanelHostImpl::setShortcuts(const QList<PanelShortcut> &shortcuts)
{
    if (!m_window)
        return;

    // Набор объявляется целиком, поэтому прежние действия снимаются, а не сверяются
    // поштучно: разница между двумя списками не стоит того, чтобы её вычислять, а
    // пересоздание пары QAction ничего не стоит.
    qDeleteAll(m_actions);
    m_actions.clear();
    m_shortcuts = shortcuts;

    for (const PanelShortcut &shortcut : shortcuts) {
        const QString fullId = QStringLiteral("%1.%2").arg(m_pluginId, shortcut.id);

        auto *action = new QAction(shortcut.title, m_window);
        action->setShortcut(shortcut.defaultSequence);
        // Оконная область, а не приложение: сочетание панели не должно срабатывать в
        // чужом диалоге поверх окна.
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this,
                [this, fullId] { Q_EMIT shortcutActivated(fullId); });

        m_window->addAction(action);
        m_actions.insert(fullId, action);
    }
}

QList<PanelShortcut> PanelHostImpl::configurableShortcuts() const
{
    QList<PanelShortcut> result;
    for (const PanelShortcut &shortcut : m_shortcuts) {
        if (shortcut.userConfigurable)
            result.append(shortcut);
    }
    return result;
}

void PanelHostImpl::applyShortcutOverride(const QString &shortcutId,
                                          const QKeySequence &sequence)
{
    if (QAction *action = m_actions.value(shortcutId))
        action->setShortcut(sequence);
}

// --- Терминал: запись ------------------------------------------------------------------------

void PanelHostImpl::appendToTerminal(const QString &text)
{
    if (m_context.session)
        m_context.session->buffer()->appendSystemMessage(text);
}

void PanelHostImpl::injectReceived(const QByteArray &data)
{
    if (!m_context.session)
        return;
    // Прямо в буфер, минуя сессию: иначе звено цепочки, пишущее сюда из filterIncoming(),
    // отправило бы данные на второй круг через себя же.
    m_context.session->buffer()->append(data, DataDirection::Rx, 0, /*terminatesLine=*/true);
}

void PanelHostImpl::clearTerminal()
{
    if (m_context.session)
        m_context.session->buffer()->clear();
}

void PanelHostImpl::scrollTerminalToBottom()
{
    if (TerminalView *terminal = m_window ? m_window->terminalView() : nullptr)
        terminal->scrollToBottom();
}

bool PanelHostImpl::showDocument(const QString &filePath, const QString &title)
{
    return m_window && m_window->showDocument(filePath, title);
}

// --- Терминал: управление показом --------------------------------------------------------------

void PanelHostImpl::setHighlightRules(const HighlightRules &rules)
{
    if (TerminalView *terminal = m_window ? m_window->terminalView() : nullptr)
        terminal->setHighlightRules(rules);
}

void PanelHostImpl::setSearchPattern(const QString &pattern, SearchOptions options)
{
    if (TerminalView *terminal = m_window ? m_window->terminalView() : nullptr) {
        terminal->setSearchPattern(pattern, options.regularExpression, options.caseSensitive,
                                   options.wholeWords);
    }
}

void PanelHostImpl::setFilterEnabled(bool enabled)
{
    if (TerminalView *terminal = m_window ? m_window->terminalView() : nullptr)
        terminal->setFilterEnabled(enabled);
}

void PanelHostImpl::findNext()
{
    if (TerminalView *terminal = m_window ? m_window->terminalView() : nullptr)
        terminal->findNext();
}

void PanelHostImpl::findPrevious()
{
    if (TerminalView *terminal = m_window ? m_window->terminalView() : nullptr)
        terminal->findPrevious();
}

int PanelHostImpl::matchCount() const
{
    TerminalView *terminal = m_window ? m_window->terminalView() : nullptr;
    return terminal ? terminal->matchCount() : 0;
}

int PanelHostImpl::currentMatch() const
{
    TerminalView *terminal = m_window ? m_window->terminalView() : nullptr;
    return terminal ? terminal->currentMatch() : 0;
}

QString PanelHostImpl::selectedText() const
{
    TerminalView *terminal = m_window ? m_window->terminalView() : nullptr;
    return terminal ? terminal->selectedText() : QString();
}

// --- Терминал: чтение строк ------------------------------------------------------------------

qint64 PanelHostImpl::firstLineNumber() const
{
    return m_context.session ? m_context.session->buffer()->firstLineNumber() : 0;
}

qint64 PanelHostImpl::nextLineNumber() const
{
    return m_context.session ? m_context.session->buffer()->nextLineNumber() : 0;
}

bool PanelHostImpl::line(qint64 number, TerminalLine *out) const
{
    if (!out || !m_context.session)
        return false;

    const TerminalBuffer::Line *source = m_context.session->buffer()->line(number);
    if (!source)
        return false;

    // Отрезки оформления не копируются намеренно: раскраска — дело виджета, а тип отрезка
    // в ABI замораживать незачем.
    out->text = source->text;
    out->raw = source->raw;
    out->monotonicNs = source->monotonicNs;
    out->wallClock = source->wallClock;
    out->direction = source->direction;
    out->source = source->source;
    out->complete = source->complete;
    return true;
}

TerminalGutterSettings PanelHostImpl::terminalGutterSettings() const
{
    TerminalGutterSettings settings;
    TerminalView *terminal = m_window ? m_window->terminalView() : nullptr;
    if (!terminal)
        return settings;

    settings.showLineNumbers = terminal->showLineNumbers();
    settings.showSource = terminal->showSource();
    settings.showTimestamps = terminal->showTimestamps();
    settings.relativeTimestamps = terminal->relativeTimestamps();
    settings.showDirection = terminal->showDirection();
    settings.lineNumberOrigin = terminal->lineNumberOrigin();
    settings.timestampFormat = terminal->timestampFormat();
    return settings;
}

// --- Оформление ---------------------------------------------------------------------------

QColor PanelHostImpl::color(ColorRole role) const
{
    if (!m_context.theme)
        return {};

    const ThemeColors &c = m_context.theme->colors();
    switch (role) {
    case ColorRole::Window:        return c.window;
    case ColorRole::Panel:         return c.panel;
    case ColorRole::Base:          return c.base;
    case ColorRole::Border:        return c.border;
    case ColorRole::ControlBorder: return c.controlBorder;
    case ColorRole::Text:          return c.text;
    case ColorRole::TextMuted:     return c.textMuted;
    case ColorRole::Accent:        return c.accent;
    case ColorRole::AccentText:    return c.accentText;
    case ColorRole::AccentSolid:   return c.accentSolid;
    case ColorRole::Selection:     return c.selection;
    case ColorRole::Hover:         return c.hover;
    case ColorRole::Pressed:       return c.pressed;
    case ColorRole::Rx:            return c.rxText;
    case ColorRole::Tx:            return c.txText;
    case ColorRole::Error:         return c.errorText;
    case ColorRole::Ok:            return c.okText;
    }
    return {};
}

int PanelHostImpl::metric(Metric which) const
{
    const ThemeMetrics &m = ThemeManager::metrics();
    switch (which) {
    case Metric::Gap:            return m.gap;
    case Metric::Radius:         return m.radius;
    case Metric::CardRadius:     return m.cardRadius;
    case Metric::ControlHeight:  return m.controlHeight;
    case Metric::ButtonMinWidth: return m.buttonMinWidth;
    case Metric::PadV:           return m.padV;
    case Metric::PadH:           return m.padH;
    case Metric::ScrollBarWidth: return m.scrollBarWidth;
    case Metric::FontPointSize:  return m.fontPointSize;
    case Metric::TitlePointSize: return m.titlePointSize;
    case Metric::SmallPointSize: return m.smallPointSize;
    }
    return 0;
}

bool PanelHostImpl::isDarkTheme() const
{
    return m_context.theme && m_context.theme->theme() == ThemeManager::Theme::Dark;
}

QIcon PanelHostImpl::icon(char32_t glyph, int size) const
{
    return MdiIcons::icon(glyph, size);
}

QIcon PanelHostImpl::mutedIcon(char32_t glyph, int size) const
{
    return MdiIcons::mutedIcon(glyph, size);
}

// --- Толчки сигналов от окна --------------------------------------------------------------

void PanelHostImpl::notifyChannelState(ChannelState state)
{
    Q_EMIT channelStateChanged(state);
}

void PanelHostImpl::notifySecondInterfaceState(bool dualTransportEnabled,
                                               bool secondInterfaceAvailable)
{
    Q_EMIT secondInterfaceStateChanged(dualTransportEnabled, secondInterfaceAvailable);
}

void PanelHostImpl::notifyThemeChanged()
{
    Q_EMIT themeChanged();
}

void PanelHostImpl::notifySettingsReset()
{
    Q_EMIT settingsReset();
}

void PanelHostImpl::notifyAboutToClose()
{
    Q_EMIT aboutToClose();
}

void PanelHostImpl::notifySendBarSave(const QString &text, DataCodec::Format format,
                                      DataCodec::Termination termination)
{
    Q_EMIT sendBarSaveRequested(text, format, termination);
}

} // namespace spotty
