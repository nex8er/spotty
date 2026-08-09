/**
 * \file SendBar.h
 * \brief Нижняя строка отправки данных.
 */
#pragma once

#include <spotty/data/DataCodec.h>

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
    /// \brief Получатель данных в режиме двух интерфейсов.
    enum class SendTarget {
        InterfaceA,
        InterfaceB,
        Both,
    };

    /**
     * \brief Конструктор.
     * \param history Хранилище истории. Виджет им не владеет; может быть `nullptr`.
     */
    explicit SendBar(HistoryStore *history, QWidget *parent = nullptr);

    DataCodec::Format format() const;
    void setFormat(DataCodec::Format format);

    DataCodec::Termination termination() const;
    void setTermination(DataCodec::Termination termination);

    /// \brief Переключить доступность выбора получателя для режима двух интерфейсов.
    void setDualTransport(bool enabled);

    /// \brief Указать, в какие открытые интерфейсы сейчас можно отправлять.
    void setInterfaceAvailability(bool interfaceA, bool interfaceB);

    void setSendTarget(SendTarget target);
    SendTarget sendTarget() const { return m_sendTarget; }

    /// \brief Передать фокус полю ввода.
    void focusInput();

    /// \brief Заменить содержимое поля ввода. Используется генератором.
    void setText(const QString &text);

Q_SIGNALS:
    /**
     * \brief Пользователь отправляет данные.
     * \param data Уже закодированные байты вместе с терминацией.
     */
    void sendRequested(const QByteArray &data, SendTarget target);

    /// \brief Пользователь сменил получателя в режиме двух интерфейсов.
    void sendTargetChanged(SendTarget target);

    /// \brief Изменился формат или терминация — стоит запомнить в настройках.
    void optionsChanged();

    /**
     * \brief Пользователь просит сохранить набранное как макрос.
     *
     * Полоса отправки не знает о макросах — они живут в плагине, и связывать её с ним
     * напрямую значило бы вернуть в окно ту самую поимённую связь, от которой ушли.
     * Окно передаёт просьбу тому, кто её примет.
     */
    void makeMacroRequested(const QString &text, int format, int termination);

private:
    /// \brief Закодировать введённое и отправить.
    void submit();

    /// \brief Дополнить ввод по истории (Tab).
    /**
     * \brief Подставить следующий вариант из истории по набранному началу.
     * \param backwards Идти к более старым записям, а не к более новым.
     */
    void completeFromHistory(bool backwards);

    /// \brief Прекратить перебор вариантов: набранное начало больше не действует.
    void resetCompletion();

    /// \brief Шаг по истории: -1 назад, +1 вперёд.
    void stepHistory(int direction);

    /// \brief Показать или убрать сообщение об ошибке кодирования.
    void showError(const QString &message);

    /// \brief Открыть меню ввода по глобальной позиции курсора.
    void showInputContextMenu(const QPoint &globalPosition);

    /// \brief Обработчик клавиш и контекстного меню поля ввода.
    bool eventFilter(QObject *watched, QEvent *event) override;

    HistoryStore *m_history;

    QLineEdit *m_input = nullptr;
    QComboBox *m_format = nullptr;
    QComboBox *m_termination = nullptr;
    QPushButton *m_send = nullptr;
    QLabel *m_error = nullptr;

    bool m_dualTransport = false;
    bool m_interfaceAAvailable = false;
    bool m_interfaceBAvailable = false;
    SendTarget m_sendTarget = SendTarget::InterfaceA;

    /**
     * \brief Позиция в истории при переборе.
     *
     * Равна размеру истории, когда перебор не идёт, — то есть «мы в конце, на своём
     * тексте».
     */
    int m_historyIndex = 0;

    /// \name Перебор вариантов дополнения по Tab
    /// Начало, набранное человеком, запоминается при первом Tab и не меняется, пока идёт
    /// перебор: иначе каждый следующий Tab искал бы продолжение уже подставленного
    /// варианта и перебор схлопывался бы на первом же совпадении.
    /// @{
    QString m_completionStem;
    QStringList m_completionMatches;
    int m_completionIndex = -1;
    /// @}

    /// \brief Набранное до входа в историю, чтобы вернуть его при выходе.
    QString m_draft;
};

} // namespace spotty
