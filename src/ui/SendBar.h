/**
 * \file SendBar.h
 * \brief Нижняя строка отправки данных.
 */
#pragma once

#include <terminal/DataCodec.h>

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace spotty {

class HistoryStore;

/**
 * \class SendBar
 * \brief Ввод и отправка данных: формат, терминация, история, автодополнение.
 *
 * \par История и клавиши
 *
 * - **Вверх** и **Вниз** — перебор истории. Начатый, но не отправленный текст сохраняется
 *   и возвращается при выходе из истории вниз: потерять набранное на полпути неприятно.
 * - **Tab** — дополнение до наибольшего общего продолжения подходящих записей. Если
 *   вариантов несколько, они показываются подсказкой рядом.
 *
 * История ведётся в файле, поэтому переживает перезапуск.
 *
 * \see spotty::HistoryStore
 */
class SendBar : public QWidget
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param history Хранилище истории. Виджет им не владеет; может быть `nullptr`.
     */
    explicit SendBar(HistoryStore *history, QWidget *parent = nullptr);

    DataCodec::Format format() const;
    void setFormat(DataCodec::Format format);

    DataCodec::Termination termination() const;
    void setTermination(DataCodec::Termination termination);

    /// \brief Разрешить отправку. Выключается, когда канал закрыт.
    void setSendEnabled(bool enabled);

    /// \brief Передать фокус полю ввода.
    void focusInput();

    /// \brief Заменить содержимое поля ввода. Используется генератором.
    void setText(const QString &text);

Q_SIGNALS:
    /**
     * \brief Пользователь отправляет данные.
     * \param data Уже закодированные байты вместе с терминацией.
     */
    void sendRequested(const QByteArray &data);

    /// \brief Изменился формат или терминация — стоит запомнить в настройках.
    void optionsChanged();

private:
    /// \brief Закодировать введённое и отправить.
    void submit();

    /// \brief Дополнить ввод по истории (Tab).
    void completeFromHistory();

    /// \brief Шаг по истории: -1 назад, +1 вперёд.
    void stepHistory(int direction);

    /// \brief Показать или убрать сообщение об ошибке кодирования.
    void showError(const QString &message);

    /// \brief Обработчик клавиш поля ввода.
    bool eventFilter(QObject *watched, QEvent *event) override;

    HistoryStore *m_history;

    QLineEdit *m_input = nullptr;
    QComboBox *m_format = nullptr;
    QComboBox *m_termination = nullptr;
    QPushButton *m_send = nullptr;
    QLabel *m_error = nullptr;

    /**
     * \brief Позиция в истории при переборе.
     *
     * Равна размеру истории, когда перебор не идёт, — то есть «мы в конце, на своём
     * тексте».
     */
    int m_historyIndex = 0;

    /// \brief Набранное до входа в историю, чтобы вернуть его при выходе.
    QString m_draft;
};

} // namespace spotty
