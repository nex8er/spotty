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
     */
    SchemaForm(const SettingsSchema &schema, const QVariantMap &values,
               QWidget *parent = nullptr);

    /// \return Значения из редакторов, приведённые к типам умолчаний схемы.
    QVariantMap values() const;

Q_SIGNALS:
    /// \brief Пользователь изменил хоть одно поле.
    void valueChanged();

private:
    /// \brief Создать редактор под один тип поля.
    QWidget *createEditor(const SettingsField &field, const QVariant &value);

    SettingsSchema m_schema;
    QVariantMap m_initialValues;
    QHash<QString, QWidget *> m_editors;
};

} // namespace spotty
