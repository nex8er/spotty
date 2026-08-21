/**
 * \file JsonGenChannel.h
 * \brief Виртуальный канал, выдающий телеметрию в JSON для отладки разбора.
 */
#pragma once

#include <spotty/api/IInterfaceChannel.h>

#include <QElapsedTimer>
#include <QJsonObject>

class QTimer;

namespace spotty {

/**
 * \class JsonGenChannel
 * \brief Источник документов JSON во всех формах, на которых ломается разбор.
 *
 * \par Что именно он воспроизводит
 *
 * Форму документа (плоский объект, вложенный, массив объектов и чередование всех трёх),
 * разную частоту у разных полей, помехи — оборванный JSON и обычные текстовые строки, —
 * вывод с отступами на несколько строк и меняющуюся на ходу структуру, где ключи появляются
 * и исчезают.
 *
 * \par Почему «поле не пришло» означает выброшено из документа
 *
 * Разную частоту нельзя изобразить, оставив поле в документе с прежним значением: путь всё
 * равно приходит каждый раз, и частота у всех полей выходит одинаковой. Поле, которому не
 * подошёл черёд, из документа убирается целиком — тогда и только тогда счётчик частоты у
 * него отстаёт от соседей.
 *
 * \see spotty::JsonGenPlugin
 */
class JsonGenChannel : public IInterfaceChannel
{
    Q_OBJECT

public:
    explicit JsonGenChannel(QObject *parent = nullptr);

    /// \copydoc spotty::IInterfaceChannel::open
    bool open(const QVariantMap &settings, QString *error) override;

    /// \copydoc spotty::IInterfaceChannel::close
    void close() override;

    /// \brief Отбрасывает отправленное: канал только выдаёт телеметрию, команд не принимает.
    qint64 write(const QByteArray &data) override;

    /// \copydoc spotty::IInterfaceChannel::state
    ChannelState state() const override { return m_state; }

    /// \copydoc spotty::IInterfaceChannel::applySettings
    bool applySettings(const QVariantMap &settings) override;

private:
    /// \brief Собрать и выдать очередной документ вместе с сопутствующим мусором.
    void emitDocument();

    /// \name Построение документа
    /// @{

    /// \brief Плоский объект из полей, которым подошёл черёд.
    QJsonObject flatObject(double t) const;

    /// \brief Вложенный объект заданной глубины.
    QJsonObject nestedObject(double t) const;

    /// \brief Массив объектов; идентификатор — счётчик либо случайное число.
    QJsonArray objectArray(double t) const;

    /**
     * \brief Приходит ли поле в этом документе.
     * \param field Номер поля; от него зависит, к какой из трёх групп оно отнесено.
     *
     * Каждое третье поле — быстрое, следующее — среднее (раз в пять документов), остальные
     * медленные (раз в двадцать пять).
     */
    bool fieldIsDue(int field) const;

    /// \brief Имя поля с учётом дрейфа структуры.
    QString fieldName(int field) const;

    /// @}

    /// \brief Отправить текст, при необходимости разбив его на два пакета.
    void emitText(const QString &text);

    /// \brief Обрубить или испортить готовый текст документа.
    QString corrupt(const QString &text) const;

    /// \brief Сменить состояние и известить, если оно действительно изменилось.
    void setState(ChannelState state, const QString &detail = {});

    ChannelState m_state = ChannelState::Closed;
    QVariantMap m_settings;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock; ///< С момента open(): даёт t для формул значений.
    quint64 m_counter = 0; ///< Число выданных документов; от него считается дрейф и частоты.
};

} // namespace spotty
