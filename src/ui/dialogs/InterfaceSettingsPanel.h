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
class QTimer;
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

    /**
     * \brief Подсветить обязательные поля текущего устройства как ошибочные.
     * \param fieldKeys Ключи полей схемы (SettingsField::key). Устройство, к которому они
     *        относятся, — то, что уже выбрано в панели (см. selectInterface()); вызывающая
     *        сторона выбирает устройство первым.
     *
     * Подсветка держится, пока showEntry() не переключит панель на другое устройство или
     * пользователь не заполнит поле — commitSchemaValues() снимает флаг с полей, у которых
     * появилось непустое значение.
     */
    void flagInvalidFields(const QStringList &fieldKeys);

protected:
    /// \brief Возобновить опрос живых полей: панель снова на экране.
    void showEvent(QShowEvent *event) override;

    /// \brief Прекратить опрос: закрытый диалог не должен держать занятой шину устройства.
    void hideEvent(QHideEvent *event) override;

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

    /**
     * \brief Редактор для поля Choice со списком, слишком большим для обычного выпадения.
     *
     * QComboBox с тысячами addItem() заметно тормозит при открытии, а бесконечная
     * прокрутка по алфавиту всё равно бесполезна человеку. Вместо этого — поле ввода с
     * живым поиском: подсказка ограничена несколькими совпадениями и уточняется по мере
     * набора текста.
     */
    QWidget *createLiveSearchEditor(const SettingsField &field, const QVariant &value);

    /// \brief Собрать значения всех редакторов схемы и записать их в реестр.
    void commitSchemaValues();

    /**
     * \brief Спросить у плагина пункты для полей с SettingsField::live и обновить списки.
     *
     * Вызывается по таймеру, пока панель на экране. Сам факт вызова для плагина означает
     * «опрос ещё нужен» — см. spotty::IInterfacePlugin::liveOptions().
     */
    void refreshLiveOptions();

    /**
     * \brief Переписать пункты одного списка, не потеряв выбранного пользователем.
     * \param key Ключ поля схемы.
     * \param options Пункты, известные плагину сейчас.
     *
     * Ничего не делает, если список не изменился: перезаполнение открытого QComboBox
     * закрывает выпадение и сбрасывает набранный текст, а приходит сюда раз в секунду.
     */
    void applyLiveOptions(const QString &key, const QList<SettingsOption> &options);

    /// \brief Запустить или остановить опрос живых полей по состоянию панели и схемы.
    void updateLiveTimer();

    /// \brief Перенести #m_invalidFields на редакторы: свойство `fieldInvalid` + перекраска.
    void applyInvalidFieldStyling();

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

    /**
     * \brief Таймер опроса полей с SettingsField::live.
     *
     * Идёт, только пока панель видима и у текущего устройства есть хоть одно такое поле:
     * прекратившиеся вызовы liveOptions() — единственный способ сказать плагину, что опрос
     * больше не нужен и шину можно отпустить.
     */
    QTimer *m_liveTimer = nullptr;

    /// \brief Ключи полей текущей схемы с SettingsField::live.
    QStringList m_liveFields;

    /// \brief Идёт перестроение полей — гасит обработчики, чтобы не писать в реестр то,
    ///        что сам же из него прочитал.
    bool m_populating = false;

    /**
     * \brief Ключи полей текущего устройства, подсвеченных как ошибочные.
     *
     * Привязаны к устройству, а не абсолютны: showEntry() очищает список при переключении
     * на другое устройство — чужая подсветка не должна пережить смену выбора в списке.
     */
    QStringList m_invalidFields;
    QString m_invalidFieldsForId; ///< Устройство, к которому относится #m_invalidFields.
};

} // namespace spotty
