/**
 * \file LoopbackChannel.h
 * \brief Виртуальный канал, не требующий железа.
 */
#pragma once

#include <spotty/api/IInterfaceChannel.h>

class QTimer;

namespace spotty {

/**
 * \class LoopbackChannel
 * \brief Транспорт, которому не нужно никакое оборудование.
 *
 * \par Зачем он существует
 *
 * Две причины, обе важные:
 *
 * 1. **Разработка без железа.** Терминал, логирование и макросы можно писать и
 *    показывать, не подключая устройство. Режим «генерация данных» выдаёт поток строк с
 *    последовательностями ANSI, на котором удобно отлаживать разбор и прокрутку.
 * 2. **Проверка API на общность.** Плагин намеренно написан раньше UART. Если SDK удобно
 *    ложится на транспорт, устроенный совсем иначе, чем последовательный порт, значит
 *    он не выродился в «интерфейс к UART под другим именем».
 *
 * \par Режимы
 *
 * - `echo` — возвращает отправленное с настраиваемой задержкой;
 * - `chatter` — сам выдаёт строки с заданным периодом;
 * - `silent` — не делает ничего, полезен для проверки состояний.
 *
 * \see spotty::LoopbackPlugin
 */
class LoopbackChannel : public IInterfaceChannel
{
    Q_OBJECT

public:
    explicit LoopbackChannel(QObject *parent = nullptr);

    /// \copydoc spotty::IInterfaceChannel::open
    bool open(const QVariantMap &settings, QString *error) override;

    /// \copydoc spotty::IInterfaceChannel::close
    void close() override;

    /// \copydoc spotty::IInterfaceChannel::write
    qint64 write(const QByteArray &data) override;

    /// \copydoc spotty::IInterfaceChannel::state
    ChannelState state() const override { return m_state; }

    /**
     * \brief Применить настройки на лету.
     *
     * Реализовано закрытием и повторным открытием: содержимое терминала при этом не
     * теряется, а именно ради возможности покрутить период генерации, не теряя вывод,
     * метод здесь и переопределён.
     */
    bool applySettings(const QVariantMap &settings) override;

    /**
     * \brief Мнимые линии управления.
     *
     * Выходы отражаются во входы — виртуальный аналог заглушки-петли на разъёме. Нужно,
     * чтобы индикаторы линий в интерфейсе можно было проверить до появления железа.
     */
    QVariantMap controlLines() const override;

    /// \copydoc spotty::IInterfaceChannel::setControlLine
    bool setControlLine(const QString &name, bool asserted) override;

private:
    /// \brief Выдать очередную строку в режиме генерации.
    void emitChatterLine();

    /// \brief Сменить состояние и известить, если оно действительно изменилось.
    void setState(ChannelState state, const QString &detail = {});

    /// \brief Обернуть текст в последовательности ANSI, если это включено в настройках.
    QByteArray decorate(const QByteArray &text) const;

    ChannelState m_state = ChannelState::Closed;
    QVariantMap m_settings;             ///< Настройки, с которыми открыт канал.
    QTimer *m_chatterTimer = nullptr;   ///< Таймер генерации; nullptr вне режима chatter.
    quint64 m_counter = 0;              ///< Счётчик выданных строк.
    bool m_dtr = false;                 ///< Состояние выходной линии DTR.
    bool m_rts = false;                 ///< Состояние выходной линии RTS.
};

} // namespace spotty
