/**
 * \file ActionPromptDialog.h
 * \brief Окно вопроса перед действием плагина: имя и пароль, которые нигде не сохраняются.
 */
#pragma once

#include <spotty/api/SettingsSchema.h>

#include <QDialog>
#include <QVariantMap>

class QDialogButtonBox;

namespace spotty {

class SchemaForm;

/**
 * \class ActionPromptDialog
 * \brief Модальная форма по схеме, которую вернул spotty::IInterfacePlugin::promptSchema().
 *
 * Отдельное окно, а не поля в общих настройках, по одной причине: то, что здесь вводят, не
 * принадлежит устройству. Имя и пароль для ssh нужны одному нажатию кнопки, и в настройках
 * подключения, которые лежат в файле и переезжают вместе с программой, им не место.
 *
 * Редкие поля (spotty::SettingsField::advanced) свёрнуты, пароли скрыты. Кнопка «OK» недоступна,
 * пока не заполнены обязательные поля.
 */
class ActionPromptDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * \param title Заголовок окна — обычно подпись нажатой кнопки.
     * \param schema Что спросить.
     * \param initial Чем заполнить поля (прошлый ввод в этом же окне настроек).
     */
    ActionPromptDialog(const QString &title, const SettingsSchema &schema, const QVariantMap &initial,
                       QWidget *parent = nullptr);

    /// \return Введённые значения; только ключи схемы.
    QVariantMap values() const;

private:
    /// \brief Включить «OK», когда обязательные поля заполнены.
    void updateOkButton();

    SettingsSchema m_schema;
    SchemaForm *m_form = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace spotty
