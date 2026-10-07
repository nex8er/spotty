/**
 * \file UartProxyChannel.h
 * \brief Канал перехвата: реальный порт с одной стороны, виртуальный — с другой.
 */
#pragma once

#include <spotty/api/IInterfaceChannel.h>

#include <QElapsedTimer>

#include <functional>
#include <memory>

class QSerialPort;

namespace spotty {

class VirtualPort;

/**
 * \class UartProxyChannel
 * \brief Переправляет байты между реальным и виртуальным портами и сообщает ядру обо всём.
 *
 * \par Направления
 *
 * Они именуются так же, как у обычного порта, — с точки зрения «хозяина» порта:
 * - от устройства к программе — **приём** (spotty::IInterfaceChannel::dataReceived);
 * - от программы к устройству — **передача** (spotty::IInterfaceChannel::dataTransmitted).
 *
 * Обе отметки времени берутся одними часами, чтобы терминал расставил направления в том
 * порядке, в каком байты прошли по проводу.
 *
 * \par Что в канал не входит
 *
 * Линии DTR/RTS и BREAK программа на виртуальном порту выставляет, но реальному порту они
 * не передаются: pty о них не сообщает вовсе, а у пары com0com нужен отдельный опрос.
 * Для устройства, которое сбрасывается по DTR, это значит, что программа плату не
 * перезагрузит — что для перехвата скорее достоинство.
 */
class UartProxyChannel : public IInterfaceChannel
{
    Q_OBJECT

public:
    explicit UartProxyChannel(QObject *parent = nullptr);
    ~UartProxyChannel() override;

    bool open(const QVariantMap &settings, QString *error) override;
    void close() override;

    /**
     * \brief Отправить байты устройству от имени Spotty.
     *
     * Вклинивается в обмен наравне с программой: полезно, чтобы проверить, как реагирует
     * устройство, не трогая чужую программу.
     */
    qint64 write(const QByteArray &data) override;

    ChannelState state() const override { return m_state; }

    /**
     * \brief Сменить скорость и формат кадра без разрыва.
     * \return `false`, если изменились сами порты: тогда ядро переоткроет канал.
     */
    bool applySettings(const QVariantMap &settings) override;

    /**
     * \brief Кому сообщить номер пары com0com, созданной при открытии (только Windows).
     *
     * Пару, которую завёл Spotty, при выходе нужно убрать, а чужую — нельзя. Канал знает,
     * что создал её сам, но живёт меньше программы, поэтому помнит это плагин.
     *
     * \warning Вызывается из потока ввода-вывода.
     */
    void setPairCreatedHandler(std::function<void(int)> handler)
    {
        m_pairCreated = std::move(handler);
    }

private:
    void onRealReadyRead();
    void onRealError();
    void onVirtualData(const QByteArray &data);

    /**
     * \brief Закрыть оба конца, ничего не сообщая; общая часть close() и неудачного open().
     *
     * Сами объекты портов уничтожаются отложенно: вызов может прийти из обработчика сигнала
     * того самого порта (ошибка ввода-вывода), а удалять объект изнутри его же сигнала
     * нельзя — после возврата Qt обратится к уже освобождённой памяти.
     */
    void teardown();

    /// \brief Фатальная ошибка: закрыть всё и перейти в состояние ошибки.
    void fail(const QString &message);

    /// \brief Создать виртуальный конец по настройкам и текущей платформе.
    std::unique_ptr<VirtualPort> createVirtualPort(const QVariantMap &settings,
                                                   QString *error) const;

    void setState(ChannelState state, const QString &detail = {});

    QSerialPort *m_real = nullptr;
    std::unique_ptr<VirtualPort> m_virtual;
    ChannelState m_state = ChannelState::Closed;
    QElapsedTimer m_clock;

    /// \brief Порты, с которыми канал открыт: их смена на лету невозможна.
    QString m_openedReal;
    QString m_openedVirtual;

    std::function<void(int)> m_pairCreated;
};

} // namespace spotty
