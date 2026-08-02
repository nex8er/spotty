/**
 * \file ChannelWorker.cpp
 * \brief Реализация spotty::ChannelWorker.
 */
#include "ChannelWorker.h"

#include <spotty/api/IInterfaceChannel.h>

namespace spotty {

ChannelWorker::ChannelWorker(IInterfaceChannel *channel, QObject *parent)
    : QObject(parent)
    , m_channel(channel)
{
    if (!m_channel)
        return;

    // Канал становится дочерним, чтобы уничтожиться вместе с работником — в потоке
    // ввода-вывода, где живут его таймеры и дескрипторы.
    m_channel->setParent(this);

    connect(m_channel, &IInterfaceChannel::dataReceived,
            this, &ChannelWorker::dataReceived);
    connect(m_channel, &IInterfaceChannel::stateChanged,
            this, &ChannelWorker::stateChanged);
    connect(m_channel, &IInterfaceChannel::errorOccurred,
            this, &ChannelWorker::errorOccurred);
    connect(m_channel, &IInterfaceChannel::controlLinesChanged,
            this, &ChannelWorker::pollControlLines);
}

ChannelWorker::~ChannelWorker() = default;

void ChannelWorker::open(const QVariantMap &settings)
{
    if (!m_channel)
        return;

    QString error;
    if (!m_channel->open(settings, &error)) {
        Q_EMIT openFailed(error.isEmpty() ? tr("Could not open the interface.") : error);
        return;
    }
    pollControlLines();
}

void ChannelWorker::close()
{
    if (m_channel)
        m_channel->close();
}

void ChannelWorker::write(const QByteArray &data)
{
    if (!m_channel)
        return;

    const qint64 written = m_channel->write(data);
    if (written < 0) {
        Q_EMIT errorOccurred(tr("Could not send %n byte(s).", nullptr, int(data.size())));
        return;
    }

    // Отражаем в терминал ровно столько, сколько канал действительно принял: показать
    // отправленным то, что не ушло, значит соврать в самом важном месте.
    Q_EMIT bytesWritten(written == data.size() ? data : data.left(written));
}

void ChannelWorker::applySettings(const QVariantMap &settings)
{
    if (!m_channel)
        return;

    // Канал вправе не уметь перенастраиваться на лету. Тогда закрываем и открываем
    // заново — заметно пользователю, но работает всегда.
    if (m_channel->applySettings(settings))
        return;

    m_channel->close();
    QString error;
    if (!m_channel->open(settings, &error))
        Q_EMIT openFailed(error.isEmpty() ? tr("Could not reopen the interface.") : error);
    else
        pollControlLines();
}

void ChannelWorker::setControlLine(const QString &name, bool asserted)
{
    if (m_channel && m_channel->setControlLine(name, asserted))
        pollControlLines();
}

void ChannelWorker::sendBreak(int milliseconds)
{
    if (m_channel)
        m_channel->sendBreak(milliseconds);
}

void ChannelWorker::pollControlLines()
{
    if (m_channel)
        Q_EMIT controlLinesChanged(m_channel->controlLines());
}

} // namespace spotty
