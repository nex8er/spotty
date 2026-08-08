/**
 * \file SettingsDialog.h
 * \brief Диалог настроек приложения.
 */
#pragma once

#include <settings/AppSettings.h>

#include <QDialog>
#include <QHash>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QKeySequenceEdit;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class QToolButton;

namespace spotty {

class PanelPluginRegistry;
class SchemaForm;

class InterfaceRegistry;
class InterfaceSettingsPanel;
class PluginManager;

/**
 * \struct ShortcutAction
 * \brief Действие приложения, которому можно назначить сочетание клавиш.
 */
struct ShortcutAction
{
    QString id;       ///< Устойчивый идентификатор, попадающий в настройки.
    QString title;    ///< Название для пользователя.
    QString defaultSequence;
};

/**
 * \class SettingsDialog
 * \brief Настройки приложения: список разделов слева, страницы справа.
 *
 * \par Модель правки
 *
 * Диалог правит **копию** spotty::AppSettings и отдаёт её только при подтверждении.
 * Живое применение по мере ввода выглядело бы отзывчивее, но отменить его было бы
 * нечем: «Cancel» перестал бы что-либо значить.
 *
 * \par Смена языка
 *
 * Требует перезапуска. Живая перерисовка интерфейса под другой язык в Qt требует, чтобы
 * каждый виджет реализовывал повторный перевод своих строк, а забытая строка молча
 * остаётся на старом языке. Честное «нужен перезапуск» надёжнее.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param settings Текущие настройки.
     * \param ansiPalette Действующие шестнадцать цветов ANSI — показываются, когда
     *        пользователь своих ещё не задал.
     * \param registry Реестр интерфейсов для раздела «Интерфейсы»; `nullptr` — раздел не
     *        строится (например, в тестах, где реестра нет).
     * \param plugins Менеджер плагинов; действует та же оговорка, что и для \p registry.
     * \param panels Реестр панельных плагинов: из их схем строятся отдельные разделы.
     *        `nullptr` — разделов плагинов не будет.
     * \param pluginValues Текущие настройки плагинов по их идентификаторам. Как и
     *        \p settings, это копия: правки уезжают в хранилище только после «ОК».
     * \param shortcutActions Полный список настраиваемых действий, включая объявленные
     *        панелями. Пустой список означает «только собственные действия окна».
     */
    SettingsDialog(const AppSettings &settings, const QStringList &ansiPalette,
                   InterfaceRegistry *registry = nullptr, PluginManager *plugins = nullptr,
                   PanelPluginRegistry *panels = nullptr,
                   const QHash<QString, QVariantMap> &pluginValues = {},
                   const QList<ShortcutAction> &shortcutActions = {},
                   QWidget *parent = nullptr);

    /// \brief Настройки с внесёнными правками.
    AppSettings settings() const;

    /// \return `true`, если изменение требует перезапуска приложения.
    bool requiresRestart() const;

    /**
     * \return Панель раздела «Интерфейсы», чтобы подключиться к её settingsApplied(), либо
     *         `nullptr`, если раздел не строился (реестр не был передан конструктору).
     */
    InterfaceSettingsPanel *interfacePanel() const { return m_interfacePanel; }

    /**
     * \brief Действия самого окна, которым можно назначать сочетания клавиш.
     *
     * Сочетания панелей сюда не входят: их объявляют плагины через
     * spotty::IPanelHost::setShortcuts(), и собирает их spotty::MainWindow.
     */
    static QList<ShortcutAction> builtinShortcutActions();

    /**
     * \brief Настройки плагинов, изменённые пользователем.
     * \return Карта «идентификатор плагина → значения его схемы». Пустая, если реестр не
     *         передавался или ни один плагин схемы не объявил.
     */
    QHash<QString, QVariantMap> pluginSettings() const;

Q_SIGNALS:
    /**
     * \brief Пользователь подтвердил полный сброс приложения к умолчаниям.
     *
     * В отличие от settings() — действует немедленно, а не после Ok. Диалог сам себя
     * закрывает (reject()) сразу вслед за сигналом: MainWindow — единственный, кто
     * владеет и реестром интерфейсов, и историей отправки, поэтому выполняет сброс сам.
     */
    void resetToDefaultsRequested();

private:
    QWidget *buildGeneralPage();
    QWidget *buildTerminalPage();
    QWidget *buildSendPage();
    QWidget *buildDataPage();
    QWidget *buildShortcutsPage();
    QWidget *buildInterfacesPage(InterfaceRegistry *registry, PluginManager *plugins);

    /**
     * \brief Добавить по разделу на каждый панельный плагин со схемой.
     * \param titles Названия разделов; метод дописывает в него свои.
     *
     * Раздел появляется только у плагина, объявившего непустую схему: плагину, чьи
     * настройки правятся прямо в панели, пустая страница в диалоге не нужна.
     */
    void buildPluginPages(PanelPluginRegistry *panels, QStringList &titles);

    /**
     * \brief Раздел «Plugins»: что загрузилось, что отвергнуто и где искали.
     *
     * До него отчёт spotty::PluginManager о загрузке жил только в журнале. Молча
     * пропавший плагин — самая неприятная в разборе неисправность, и список отвергнутых
     * с причинами существует ровно ради неё; показывать его стоило с самого начала.
     */
    QWidget *buildPluginsPage(PluginManager *plugins, PanelPluginRegistry *panels);

    /// \brief Обновить доступность полей, зависящих от других полей.
    void updateEnabledState();

    AppSettings m_initial;

    QListWidget *m_categories = nullptr;
    QStackedWidget *m_pages = nullptr;

    /// \brief Панель раздела «Интерфейсы»; nullptr, если раздел не строился.
    InterfaceSettingsPanel *m_interfacePanel = nullptr;

    /// \brief Формы разделов панельных плагинов по идентификатору плагина.
    QHash<QString, SchemaForm *> m_pluginForms;

    /// \brief Значения, с которыми формы плагинов были построены.
    QHash<QString, QVariantMap> m_pluginValues;

    /// \brief Действия раздела «Сочетания клавиш».
    QList<ShortcutAction> m_shortcutActions;

    // Общие
    QComboBox *m_language = nullptr;
    QComboBox *m_theme = nullptr;
    QCheckBox *m_autoOpen = nullptr;
    QCheckBox *m_singleInstance = nullptr;

    // Терминал
    QFontComboBox *m_fontFamily = nullptr;
    QSpinBox *m_fontSize = nullptr;
    QSpinBox *m_maxLines = nullptr;
    QCheckBox *m_showTimestamps = nullptr;
    QCheckBox *m_relativeTimestamps = nullptr;
    QLineEdit *m_timestampFormat = nullptr;
    QCheckBox *m_showDirection = nullptr;
    QCheckBox *m_localEcho = nullptr;
    QLineEdit *m_csvSeparator = nullptr;
    QSpinBox *m_hexBytesPerRow = nullptr;
    QComboBox *m_encoding = nullptr;
    QList<QToolButton *> m_paletteButtons;
    QToolButton *m_resetPalette = nullptr;

    // Отправка
    QComboBox *m_sendFormat = nullptr;
    QComboBox *m_sendTermination = nullptr;
    QSpinBox *m_historySize = nullptr;

    // Логи

    // Данные
    QComboBox *m_packetizerMode = nullptr;
    QSpinBox *m_packetizerTimeout = nullptr;
    QLineEdit *m_packetizerDelimiter = nullptr;
    QSpinBox *m_packetizerLength = nullptr;

    // Горячие клавиши
    QHash<QString, QKeySequenceEdit *> m_shortcutEditors;

    /// \brief Пользователь задал свою палитру в этом сеансе правки.
    bool m_paletteCustomized = false;
};

} // namespace spotty
