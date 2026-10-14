/**
 * \file CollapsibleSection.h
 * \brief Раздел формы, который по умолчанию свёрнут: заголовок со стрелкой и поля под ним.
 */
#pragma once

#include <QWidget>

class QFormLayout;
class QToolButton;

namespace spotty {

/**
 * \class CollapsibleSection
 * \brief Заголовок-кнопка «▸ Advanced» и форма, которая раскрывается по нажатию.
 *
 * Нужен там, где у настройки есть разумное умолчание и трогают её единицы
 * (spotty::SettingsField::advanced): такие поля не должны занимать место и отвлекать, но и
 * исчезать совсем им нельзя.
 *
 * Свёрнутое содержимое не удаляется и не отключается — оно просто скрыто, поэтому значения
 * его полей читаются и применяются как у видимых.
 */
class CollapsibleSection : public QWidget
{
    Q_OBJECT

public:
    /// \param title Подпись заголовка, через tr().
    explicit CollapsibleSection(const QString &title, QWidget *parent = nullptr);

    /// \return Форма, в которую нужно добавлять строки раздела.
    QFormLayout *form() const { return m_form; }

    bool isExpanded() const;
    void setExpanded(bool expanded);

Q_SIGNALS:
    /**
     * \brief Раздел раскрыт или свёрнут.
     *
     * Владельцу, который лежит в окне без прокрутки, пора подогнать размер: минимум формы с
     * переносимыми подписями Qt считает по самой узкой раскладке и занижает высоту, поэтому окно
     * само не вырастает, а поля сжимаются и налезают друг на друга.
     */
    void toggled(bool expanded);

private:
    QToolButton *m_toggle = nullptr;
    QWidget *m_content = nullptr;
    QFormLayout *m_form = nullptr;
};

} // namespace spotty
