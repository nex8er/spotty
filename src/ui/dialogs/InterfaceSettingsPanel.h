/**
 * \file InterfaceSettingsPanel.h
 * \brief Переключатель устройств и их настройки — общий widget для двух мест в UI.
 */
#pragma once

#include <spotty/api/SettingsSchema.h>

#include <QHash>
#include <QVariantMap>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace spotty {

class InterfaceRegistry;
class PluginManager;

/**
 * \class InterfaceSettingsPanel
 * \brief Список всех известных интерфейсов и настройки того, что выбран в этом списке.
 *
 * \par Одна сущность, два места
 *
 * Этот widget — единственная реализация настроек интерфейса в приложении. Кнопка
 * настроек рядом с выбором интерфейса (spotty::InterfaceBar) открывает его в отдельном
 * диалоге (spotty::InterfaceSettingsDialog) с уже выбранным текущим устройством; раздел
 * «Интерфейсы» в общих настройках (spotty::SettingsDialog) встраивает тот же widget
 * страницей. Ни то, ни другое место не знает, как устроены поля внутри — оба лишь решают,
 * какое устройство выбрать первым.
 *
 * \par Список
 *
 * Показывает spotty::InterfaceRegistry::entries() целиком: и присутствующие, и виденные в
 * этом сеансе, но отсутствующие сейчас, и скрытые — это единственное место, где скрытый
 * интерфейс можно найти и открыть обратно.
 *
 * \par Применение правок
 *
 * Псевдоним, скрытие и поля схемы применяются немедленно, как только их отпустили
 * (editingFinished, toggled, valueChanged...) — так же, как остальные операции
 * spotty::InterfaceRegistry. Отдельной кнопки «сохранить» нет: значит нечего было бы
 * отменять кнопкой «отмена», а прыжок между устройствами в списке тогда не создавал бы
 * вопроса «а как же несохранённые правки предыдущего».
 */
class InterfaceSettingsPanel : public QWidget
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param registry Реестр интерфейсов; должен пережить widget.
     * \param plugins Менеджер плагинов; должен пережить widget.
     * \param parent Родительский виджет.
     */
    InterfaceSettingsPanel(InterfaceRegistry *registry, PluginManager *plugins,
                           QWidget *parent = nullptr);

    /// \brief Выбрать интерфейс в списке. Неизвестный или пустой идентификатор — первый пункт.
    void selectInterface(const QString &id);

Q_SIGNALS:
    /**
     * \brief Настройки устройства только что записаны в реестр.
     * \param id Идентификатор устройства.
     *
     * Канал, уже открытый на это устройство, узнаёт о новых настройках только так —
     * реестр сам ничего не решает про открытые сессии.
     */
    void settingsApplied(const QString &id);

private:
    /// \brief Перечитать список устройств, сохранив текущий выбор, если он всё ещё есть.
    void rebuildList();

    /// \brief Показать поля выбранного устройства: псевдоним, скрытие, сведения, схема.
    void showEntry(const QString &id);

    /// \brief Убрать редакторы схемы предыдущего устройства перед тем, как построить новые.
    void clearSchemaEditors();

    /// \brief Создать редактор под одно поле схемы.
    QWidget *createEditor(const SettingsField &field, const QVariant &value);

    /// \brief Собрать значения всех редакторов схемы и записать их в реестр.
    void commitSchemaValues();

    InterfaceRegistry *m_registry;
    PluginManager *m_plugins;

    QComboBox *m_deviceCombo = nullptr;

    QLineEdit *m_alias = nullptr;
    QCheckBox *m_hidden = nullptr;
    QLabel *m_addressValue = nullptr;
    QLabel *m_vidPidValue = nullptr;

    QVBoxLayout *m_schemaLayout = nullptr; ///< Сюда вставляются группы полей схемы.
    SettingsSchema m_currentSchema;
    QHash<QString, QWidget *> m_editors; ///< Редакторы схемы текущего устройства по ключу.

    QString m_currentId;

    /// \brief Идёт перестроение полей — гасит обработчики, чтобы не писать в реестр то,
    ///        что сам же из него прочитал.
    bool m_populating = false;
};

} // namespace spotty
