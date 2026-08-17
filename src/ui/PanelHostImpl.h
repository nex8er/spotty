/**
 * \file PanelHostImpl.h
 * \brief Реализация spotty::IPanelHost поверх служб приложения.
 */
#pragma once

#include "AppContext.h"

#include <spotty/ui/IPanelHost.h>

#include <QHash>
#include <QList>
#include <QString>

class QAction;

namespace spotty {

class MainWindow;

/**
 * \class PanelHostImpl
 * \brief Приложение с точки зрения одного панельного плагина.
 *
 * \par Один экземпляр на плагин
 *
 * Не на панель: настройки, каталог данных и набор сочетаний клавиш принадлежат плагину
 * целиком, а его панели делят их между собой. Заодно это делает пространство имён
 * настроек (`plugins/<pluginId>/…`) свойством самого хоста, а не тем, что панель обязана
 * не забыть приписать к ключу.
 *
 * \par Кто чем владеет
 *
 * Хостом владеет spotty::MainWindow. Действия под сочетания клавиш создаются на главном
 * окне и уничтожаются вместе с хостом — панель их не переживёт, в отличие от прежних
 * QShortcut, которые панель макросов вешала на окно сама.
 */
class PanelHostImpl : public IPanelHost
{
    Q_OBJECT

public:
    /**
     * \param context Службы приложения.
     * \param window Главное окно: родитель действий и адресат запросов на показ панели.
     * \param pluginId Идентификатор плагина; задаёт пространство настроек и каталог данных.
     */
    PanelHostImpl(const AppContext &context, MainWindow *window, QString pluginId);
    ~PanelHostImpl() override;

    QString pluginId() const override { return m_pluginId; }

    void send(const QByteArray &data) override;
    void composeInSendBar(const QString &text, DataCodec::Format format) override;

    ChannelState channelState() const override;
    QString interfaceId() const override;
    QString interfaceName() const override;
    QString interfaceAlias() const override;

    QVariant value(const QString &key, const QVariant &fallback = {}) const override;
    void setValue(const QString &key, const QVariant &value) override;
    QString dataDir() const override;
    QString documentsDir() const override;

    void showStatusMessage(const QString &message) override;
    void activatePanel(const QString &panelId) override;
    void setShortcuts(const QList<PanelShortcut> &shortcuts) override;

    void appendToTerminal(const QString &text) override;
    void injectReceived(const QByteArray &data) override;
    void clearTerminal() override;
    bool showDocument(const QString &filePath, const QString &title) override;

    void setHighlightRules(const HighlightRules &rules) override;
    void setSearchPattern(const QString &pattern, SearchOptions options) override;
    void setFilterEnabled(bool enabled) override;
    void findNext() override;
    void findPrevious() override;
    int matchCount() const override;
    int currentMatch() const override;
    QString selectedText() const override;

    qint64 firstLineNumber() const override;
    qint64 nextLineNumber() const override;
    bool line(qint64 number, TerminalLine *out) const override;

    QColor color(ColorRole role) const override;
    int metric(Metric which) const override;
    bool isDarkTheme() const override;
    QIcon icon(char32_t glyph, int size = 20) const override;
    QIcon mutedIcon(char32_t glyph, int size = 20) const override;

    /// \name Толкают сигналы хоста; зовёт spotty::MainWindow
    /// @{
    void notifyChannelState(ChannelState state);
    void notifyThemeChanged();
    void notifySettingsReset();
    void notifyAboutToClose();
    void notifySendBarSave(const QString &text, DataCodec::Format format,
                           DataCodec::Termination termination);
    /// @}

    /**
     * \brief Сочетания, объявленные плагином, — для раздела настроек.
     *
     * Возвращаются только те, у которых PanelShortcut::userConfigurable истинно:
     * остальные правятся в самой панели, и второе место правки разошлось бы с первым.
     */
    QList<PanelShortcut> configurableShortcuts() const;

    /// \brief Применить сочетание, переопределённое пользователем.
    void applyShortcutOverride(const QString &shortcutId, const QKeySequence &sequence);

private:
    /// \brief Полный ключ настройки: `plugins/<pluginId>/<key>`.
    QString settingsKey(const QString &key) const;

    AppContext m_context;
    MainWindow *m_window = nullptr;
    QString m_pluginId;

    QList<PanelShortcut> m_shortcuts;

    /// \brief Действия под сочетания, по полному идентификатору `"<pluginId>.<id>"`.
    QHash<QString, QAction *> m_actions;
};

} // namespace spotty
