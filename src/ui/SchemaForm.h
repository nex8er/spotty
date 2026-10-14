/**
 * \file SchemaForm.h
 * \brief Форма, построенная из spotty::SettingsSchema.
 */
#pragma once

#include <spotty/api/SettingsSchema.h>

#include <QHash>
#include <QVariantMap>
#include <QWidget>

namespace spotty {

/**
 * \brief Значение, которое сейчас показывает редактор поля схемы.
 * \return Невалидный QVariant, если редактор не хранит значения (кнопка, строка сведений).
 *
 * Общее для SchemaForm и spotty::InterfaceSettingsPanel: оба строят редакторы по схеме и должны
 * одинаково читать их обратно.
 */
QVariant schemaEditorValue(const QWidget *editor, const SettingsField &field);

/**
 * \class SchemaForm
 * \brief Виджет, собранный по декларативному описанию настроек.
 *
 * \par Зачем
 *
 * Схему возвращают и плагины интерфейсов, и панельные. Строить по ней виджеты — работа
 * ядра, и делать её в каждом месте заново значило бы, что новое поле в схеме приходится
 * учить понимать дважды.
 *
 * \par Модель правки
 *
 * Форма ничего никуда не пишет сама: значения забирают через values() тогда, когда это
 * нужно вызывающему. Для диалога с кнопками «ОК» и «Отмена» это единственный правильный
 * порядок — отменённая правка не должна доехать до настроек. Тем, кто применяет сразу,
 * есть сигнал valueChanged().
 *
 * \note spotty::InterfaceSettingsPanel пока строит форму своим кодом: у него применение
 *       мгновенное, со своей защитой от перестроения из обработчика собственного сигнала.
 *       Перевод его сюда — отдельная работа, и делать её заодно значило бы менять тонкое
 *       место без нужды.
 */
class SchemaForm : public QWidget
{
    Q_OBJECT

public:
    /**
     * \param schema Описание полей.
     * \param values Текущие значения; отсутствующие берутся из умолчаний схемы.
     * \param allowSecrets Показывать поля SettingsField::Password. Для настроек панелей — нет:
     *        они пишутся на диск как есть, а Password обещает обратное. Для окна вопроса перед
     *        действием (spotty::ActionPromptDialog) — да: ввод оттуда нигде не сохраняется.
     */
    SchemaForm(const SettingsSchema &schema, const QVariantMap &values, QWidget *parent = nullptr,
               bool allowSecrets = false);

    /// \return Значения из редакторов, приведённые к типам умолчаний схемы.
    QVariantMap values() const;

Q_SIGNALS:
    /// \brief Пользователь изменил хоть одно поле.
    void valueChanged();

private:
    /// \brief Создать редактор под один тип поля.
    QWidget *createEditor(const SettingsField &field, const QVariant &value);

    SettingsSchema m_schema;
    bool m_allowSecrets = false;
    QVariantMap m_initialValues;
    QHash<QString, QWidget *> m_editors;
};

} // namespace spotty
