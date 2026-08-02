/**
 * \file InterfaceSettingsDialog.h
 * \brief Диалог настроек интерфейса, построенный по схеме плагина.
 */
#pragma once

#include <spotty/api/SettingsSchema.h>

#include <QDialog>
#include <QHash>
#include <QVariantMap>

class QLineEdit;
class QWidget;

namespace spotty {

/**
 * \class InterfaceSettingsDialog
 * \brief Диалог настроек одного интерфейса.
 *
 * \par Как он получается
 *
 * Виджеты строятся из spotty::SettingsSchema, которую вернул плагин. Плагин не пишет ни
 * строчки кода интерфейса: он объявляет, что у него настраивается, а как это выглядит —
 * забота ядра. Отсюда и требование «набор настроек зависит от плагина» выполняется само,
 * и Qt6::Widgets не попадает в зависимости SDK.
 *
 * Псевдоним интерфейса добавляется сверху отдельно: он принадлежит ядру, а не плагину, и
 * есть у любого транспорта.
 */
class InterfaceSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param title Заголовок: обычно системное имя устройства.
     * \param schema Схема настроек плагина-владельца.
     * \param values Текущие значения, уже приведённые к схеме.
     * \param alias Текущий псевдоним.
     */
    InterfaceSettingsDialog(const QString &title, const SettingsSchema &schema,
                            const QVariantMap &values, const QString &alias,
                            QWidget *parent = nullptr);

    /// \brief Значения из полей диалога.
    QVariantMap values() const;

    /// \brief Введённый псевдоним.
    QString alias() const;

private:
    /// \brief Создать редактор под одно поле схемы.
    QWidget *createEditor(const SettingsField &field, const QVariant &value);

    SettingsSchema m_schema;

    /// \brief Редакторы по ключу поля — из них собираются значения.
    QHash<QString, QWidget *> m_editors;

    QLineEdit *m_alias = nullptr;
};

} // namespace spotty
