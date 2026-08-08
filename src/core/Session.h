/**
 * \file Session.h
 * \brief Сеанс работы с одним интерфейсом.
 */
#pragma once

#include "terminal/Packetizer.h"
#include "terminal/TerminalBuffer.h"

#include <spotty/api/ChannelState.h>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QThread;
class QTimer;

namespace spotty {

class ChannelWorker;
class IDataFilter;
class InterfaceRegistry;
class PluginManager;

/**
 * \class Session
 * \brief Связывает канал, буфер терминала, пакетизатор и статистику.
 *
 * \par Что делает
 *
 * Единственное место, где сходятся плагин, реестр и терминал. Владеет потоком
 * ввода-вывода, в котором живёт канал, и переводит сырые байты в строки терминала.
 *
 * \par Потоки
 *
 * Сессия живёт в потоке интерфейса; канал — в собственном потоке через
 * spotty::ChannelWorker. Все обращения к каналу ставятся в очередь его потока, все ответы
 * приходят очередными сигналами. Прямых вызовов через границу потока нет ни одного.
 *
 * \par Автоматическое переоткрытие
 *
 * Пропажа устройства переводит канал в ChannelState::Unavailable, а не Closed, и сессия
 * запоминает, что порт был открыт. При возвращении устройства порт открывается сам.
 * Закрытый пользователем порт так не открывается — это было бы борьбой с его явным
 * решением.
 *
 * \note Мультисессионность заложена в устройство класса: ничего глобального сессия не
 *       трогает, и несколько экземпляров могут работать одновременно. Интерфейс в версии
 *       0.2.0a показывает одну.
 */
class Session : public QObject
{
    Q_OBJECT

public:
    /**
     * \struct Statistics
     * \brief Счётчики обмена.
     */
    struct Statistics
    {
        qint64 bytesReceived = 0;
        qint64 bytesSent = 0;
        double receiveRateBps = 0.0; ///< Скорость приёма, байт в секунду.
        qint64 errorCount = 0;
    };

    Session(PluginManager *plugins, InterfaceRegistry *registry, QObject *parent = nullptr);
    ~Session() override;

    /// \brief Буфер терминала. Живёт столько же, сколько сессия.
    TerminalBuffer *buffer() { return &m_buffer; }
    const TerminalBuffer *buffer() const { return &m_buffer; }

    /// \brief Правило разбиения потока на строки.
    void setPacketizerMode(Packetizer::Mode mode);
    void setPacketizerTimeout(int milliseconds);
    void setPacketizerDelimiter(const QByteArray &delimiter);
    void setPacketizerFixedLength(int length);

    /// \brief Отражать ли отправленное в терминал.
    void setEchoEnabled(bool enabled);
    bool isEchoEnabled() const { return m_echoEnabled; }

    /// \brief Идентификатор выбранного интерфейса; пустая строка — не выбран.
    QString interfaceId() const { return m_interfaceId; }

    /**
     * \brief Выбрать интерфейс.
     *
     * Открытый канал закрывается. Сам по себе выбор порт не открывает — это отдельное
     * решение пользователя.
     */
    void setInterfaceId(const QString &id);

    ChannelState state() const { return m_state; }

    /// \return `true`, если канал открыт или открывается.
    bool isActive() const;

    Statistics statistics() const;

    /// \brief Состояние входных линий: `CTS`, `DSR`, `DCD`, `RI`.
    QVariantMap controlLines() const { return m_controlLines; }

    /**
     * \brief Врезать звено в цепочку преобразования потока.
     * \param filter Звено. Владение остаётся за вызывающей стороной; оно обязано пережить
     *        сессию либо быть снятым через removeDataFilter() раньше неё.
     * \param order Место в цепочке: меньше — раньше в приёмном направлении.
     * \param name Разрешает совпадения \p order. Сюда передаётся идентификатор плагина, а
     *        не порядок регистрации: тот зависит от обхода каталогов с плагинами и потому
     *        отличается от машины к машине. Одинаковый вход должен давать одинаковый
     *        порядок звеньев везде.
     *
     * Повторная врезка того же звена ничего не делает.
     */
    void addDataFilter(IDataFilter *filter, int order, const QString &name);

    /// \brief Снять звено. Безопасно для незарегистрированного.
    void removeDataFilter(IDataFilter *filter);

public Q_SLOTS:
    /// \brief Открыть выбранный интерфейс.
    void open();

    /// \brief Закрыть канал. Автоматическое переоткрытие после этого не работает.
    void close();

    /// \brief Отправить байты.
    void send(const QByteArray &data);

    /// \brief Перечитать настройки интерфейса из реестра и применить их.
    void reloadSettings();

    /// \brief Установить состояние выходной линии.
    void setControlLine(const QString &name, bool asserted);

    /// \brief Удерживать состояние BREAK.
    void sendBreak(int milliseconds = 100);

Q_SIGNALS:
    void stateChanged(spotty::ChannelState state, const QString &detail);
    void statisticsChanged();
    void controlLinesChanged(const QVariantMap &lines);

    /// \brief Ошибка, которую стоит показать пользователю.
    void errorOccurred(const QString &message);

    /**
     * \brief Открытие не начиналось: обязательное поле настроек интерфейса пусто.
     * \param interfaceId Устройство, которое пытались открыть.
     * \param missingFieldKeys Ключи полей схемы (SettingsField::key), которые нужно
     *        заполнить — обычно один, но плагин может объявить обязательными несколько.
     *
     * Отдельный сигнал, а не просто errorOccurred(): здесь есть что показать, кроме
     * текста, — открыть настройки интерфейса и подсветить нужное поле, а не только вывести
     * сообщение об ошибке в никуда.
     */
    void requiredSettingsMissing(const QString &interfaceId, const QStringList &missingFieldKeys);

    /**
     * \brief Данные прошли через сессию — сырые, до пакетизации и разбора.
     * \param data Байты как есть.
     * \param direction Принято или отправлено.
     *
     * Отдельный сигнал вместо чтения буфера: журнал должен получать поток без изменений,
     * а буфер уже разложен на строки и лишён управляющих последовательностей там, где их
     * обработал разборщик. Подписчик — spotty::LogWriter.
     */
    void dataLogged(const QByteArray &data, spotty::DataDirection direction);

    /**
     * \brief Принятые байты вместе с отметкой чтения.
     * \param data Байты как есть, до цепочки преобразования.
     * \param monotonicNs Отметка, поставленная каналом монотонными часами.
     *
     * Отдельный сигнал, а не параметр к dataLogged(): туда попадает и передающее
     * направление, у которого отметки чтения нет вовсе. Класть в неё ноль означало бы
     * завести «магическое» значение — тот самый приём, который уже один раз сломал
     * spotty::Packetizer, где ноль был признаком «данных не было».
     */
    void dataReceived(const QByteArray &data, qint64 monotonicNs);

private Q_SLOTS:
    /// \brief Устройство появилось в системе.
    void onInterfaceAppeared(const QString &id);

    /// \brief Устройство пропало из системы.
    void onInterfaceDisappeared(const QString &id);

private:
    /// \brief Создать канал и поток ввода-вывода.
    bool createWorker();

    /// \brief Остановить поток и уничтожить канал.
    void destroyWorker();

    /// \brief Сменить состояние и известить, если оно действительно изменилось.
    void setState(ChannelState state, const QString &detail = {});

    /// \brief Принятые байты: пакетизация и укладка в буфер.
    void handleIncoming(const QByteArray &data, qint64 monotonicNs);

    /// \brief Истекла межбайтовая пауза — завершить накопленный пакет.
    void handlePacketTimeout();

    /**
     * \struct FilterSlot
     * \brief Звено цепочки вместе с тем, что задаёт его место.
     */
    struct FilterSlot
    {
        int order = 0;
        QString name;
        IDataFilter *filter = nullptr;
    };

    PluginManager *m_plugins;
    InterfaceRegistry *m_registry;

    /// \brief Цепочка преобразования, упорядоченная по паре (order, name).
    QList<FilterSlot> m_filters;

    TerminalBuffer m_buffer;
    Packetizer m_packetizer;

    QThread *m_thread = nullptr;
    ChannelWorker *m_worker = nullptr;

    QString m_interfaceId;
    ChannelState m_state = ChannelState::Closed;
    QString m_stateDetail;
    QVariantMap m_controlLines;

    /**
     * \brief Порт был открыт, когда устройство пропало.
     *
     * Отличает «пользователь закрыл» от «связь потеряна» — только во втором случае порт
     * открывается автоматически при возвращении устройства.
     */
    bool m_reopenWhenAvailable = false;

    bool m_echoEnabled = true;
    qint64 m_errorCount = 0;

    /// \brief Таймер межбайтовой паузы для Packetizer::Mode::InterByteTimeout.
    QTimer *m_packetTimer = nullptr;

    /// \brief Скользящий счётчик скорости приёма.
    QTimer *m_rateTimer = nullptr;
    qint64 m_bytesAtLastTick = 0;
    double m_receiveRateBps = 0.0;
};

} // namespace spotty
