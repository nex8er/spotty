/**
 * \file ChannelWorker.h
 * \brief Владелец канала в потоке ввода-вывода.
 */
#pragma once

#include <spotty/api/ChannelState.h>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace spotty {

class IInterfaceChannel;

/**
 * \class ChannelWorker
 * \brief Переносит вызовы канала в поток ввода-вывода.
 *
 * \par Зачем нужен отдельный класс
 *
 * SDK обещает плагину, что канал живёт в выделенном потоке. Одного `moveToThread()` для
 * этого мало: методы spotty::IInterfaceChannel — обычные виртуальные функции, а не слоты,
 * и прямой вызов `channel->open(...)` выполнился бы в потоке вызывающего, то есть в
 * потоке интерфейса. Обещание оказалось бы нарушено ровно там, где это важнее всего.
 *
 * Работник живёт в потоке ввода-вывода, владеет каналом и предоставляет слоты, которые
 * ставятся в очередь этого потока. Сессия обращается к нему только через
 * `QMetaObject::invokeMethod` с очередным соединением.
 *
 * \par Владение
 *
 * Работник владеет каналом и уничтожает его в своём деструкторе — в потоке
 * ввода-вывода. Уничтожать QObject из чужого потока нельзя: его таймеры и сокеты
 * привязаны к своему циклу событий.
 */
class ChannelWorker : public QObject
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param channel Канал, созданный плагином. Работник становится его владельцем.
     *
     * \note Конструктор выполняется в потоке создателя. Ничего, привязанного к потоку,
     *       здесь заводить нельзя — только в слотах.
     */
    explicit ChannelWorker(IInterfaceChannel *channel, QObject *parent = nullptr);
    ~ChannelWorker() override;

    /// \brief Канал. Обращаться только из потока ввода-вывода.
    IInterfaceChannel *channel() const { return m_channel; }

public Q_SLOTS:
    /// \brief Открыть канал с указанными настройками.
    void open(const QVariantMap &settings);

    /// \brief Закрыть канал.
    void close();

    /// \brief Отправить данные.
    void write(const QByteArray &data);

    /// \brief Применить настройки, по возможности без разрыва соединения.
    void applySettings(const QVariantMap &settings);

    /// \brief Установить состояние выходной линии (`"DTR"`, `"RTS"`).
    void setControlLine(const QString &name, bool asserted);

    /// \brief Удерживать состояние BREAK указанное время.
    void sendBreak(int milliseconds);

    /// \brief Опросить входные линии и сообщить результат сигналом controlLinesChanged().
    void pollControlLines();

Q_SIGNALS:
    /// \brief Открытие завершилось неудачей; \p message пригоден для показа пользователю.
    void openFailed(const QString &message);

    /// \brief Данные приняты. Пробрасывается из канала без изменений.
    void dataReceived(const QByteArray &data, qint64 monotonicNs);

    /// \brief Состояние канала изменилось.
    void stateChanged(spotty::ChannelState state, const QString &detail);

    /// \brief Восстановимая ошибка транспорта.
    void errorOccurred(const QString &message);

    /// \brief Текущее состояние входных линий.
    void controlLinesChanged(const QVariantMap &lines);

    /// \brief Данные отправлены — столько байт принял канал.
    void bytesWritten(const QByteArray &data);

private:
    IInterfaceChannel *m_channel = nullptr;
};

} // namespace spotty
