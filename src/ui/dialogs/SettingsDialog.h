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
     */
    SettingsDialog(const AppSettings &settings, const QStringList &ansiPalette,
                   InterfaceRegistry *registry = nullptr, PluginManager *plugins = nullptr,
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

    /// \brief Список действий, которым можно назначать сочетания клавиш.
    static QList<ShortcutAction> shortcutActions();

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
    QWidget *buildLoggingPage();
    QWidget *buildDataPage();
    QWidget *buildShortcutsPage();
    QWidget *buildInterfacesPage(InterfaceRegistry *registry, PluginManager *plugins);

    /// \brief Обновить доступность полей, зависящих от других полей.
    void updateEnabledState();

    AppSettings m_initial;

    QListWidget *m_categories = nullptr;
    QStackedWidget *m_pages = nullptr;

    /// \brief Панель раздела «Интерфейсы»; nullptr, если раздел не строился.
    InterfaceSettingsPanel *m_interfacePanel = nullptr;

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
    QSpinBox *m_hexBytesPerRow = nullptr;
    QComboBox *m_encoding = nullptr;
    QList<QToolButton *> m_paletteButtons;
    QToolButton *m_resetPalette = nullptr;

    // Отправка
    QComboBox *m_sendFormat = nullptr;
    QComboBox *m_sendTermination = nullptr;
    QSpinBox *m_historySize = nullptr;

    // Логи
    QLineEdit *m_logDirectory = nullptr;
    QLineEdit *m_logTemplate = nullptr;
    QCheckBox *m_logFilterAnsi = nullptr;
    QCheckBox *m_logIncludeTx = nullptr;
    QCheckBox *m_logAutoStart = nullptr;

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
