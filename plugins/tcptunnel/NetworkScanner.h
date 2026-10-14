/**
 * \file NetworkScanner.h
 * \brief Поиск агентов spotty-uartd в локальной сети.
 */
#pragma once

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <deque>

class QTcpSocket;

namespace spotty {

/**
 * \class NetworkScanner
 * \brief Опрашивает TCP-порты на заданных адресах и запоминает, где что открыто.
 *
 * \par Что считается находкой
 *
 * Открытый порт, и только. Агент не говорит первым и протокола у него нет — по соединению
 * нельзя отличить его от любого другого сервиса на 7777, и выдавать находку за «агента» было
 * бы враньём. Пользователь выбирает из найденного сам, а имя машины рядом помогает
 * узнать свою.
 *
 * \par Порты подряд
 *
 * Каждый следующий агент на одной машине занимает порт на единицу больше (7777, 7778…).
 * Поэтому у хоста, где открыт первый порт, проверяется следующий, и так до первого закрытого
 * или до kMaxPorts. Хосты, у которых закрыт первый, дальше не опрашиваются: проверять 7778
 * на каждом из тысячи адресов значило бы утроить время сканирования ради случая, которого
 * не бывает.
 *
 * \par Потоки
 *
 * Сокеты асинхронные, объект живёт в потоке UI и не блокирует его: десятки соединений в
 * полёте обслуживает обычная очередь событий. Параллельных соединений не больше
 * kConcurrency — у процесса, запущенного из Dock или launchd, по умолчанию всего 256
 * дескрипторов на всё, и сотня сканирующих сокетов оставила бы приложению пустое место
 * под остальное.
 */
class NetworkScanner : public QObject
{
    Q_OBJECT

public:
    /// \brief Найденный хост и открытые на нём порты.
    struct Host
    {
        QHostAddress address;
        QList<int> ports; ///< По возрастанию, начиная с первого проверявшегося.
        QString name;     ///< Имя из обратного DNS; пусто, если не нашлось или не спрашивали.
    };

    /// \brief Параметры одного прохода.
    struct Options
    {
        QList<QHostAddress> targets; ///< Какие адреса опрашивать.
        int firstPort = 7777;        ///< С какого порта начинать у каждого хоста.

        /**
         * \brief Сколько портов подряд проверять у хоста, где открыт первый.
         *
         * Четыре — сам первый и три следующих (7777…7780): три агента на одной плате уже редкость.
         */
        int maxPorts = 4;
        bool verifyHosts = true;     ///< Проверять хост контрольными портами (см. controlPorts).

        /**
         * \brief Порты, на которых заведомо никто не слушает.
         *
         * Хост, где открыт первый порт, проверяется ими: если соединение принято и там, хост
         * «открыт на всём» — это роутер, VPN или прозрачный прокси, который принимает соединения
         * за кого угодно, и в результат такой хост не попадает. Пусто — выбрать самому (порт 1 и
         * порт на тысячу выше первого).
         */
        QList<int> controlPorts;
        int timeoutMs = 500;         ///< Сколько ждать ответа на одно соединение.
        int concurrency = 64;        ///< Сколько соединений держать в полёте разом.
        bool resolveNames = true;    ///< Спрашивать у DNS имя найденного хоста.
    };

    /// \brief Потолок на число адресов в одном проходе: больше — это уже не сканирование комнаты.
    static constexpr int kMaxTargets = 4096;

    explicit NetworkScanner(QObject *parent = nullptr);
    ~NetworkScanner() override;

    /**
     * \brief Разобрать диапазон, введённый пользователем.
     * \param text Адреса IPv4 и сети в записи CIDR через запятую, точку с запятой или пробел:
     *        `192.168.1.0/24, 10.0.0.5`.
     * \param targets Сюда попадут адреса; дубликаты отброшены, порядок сохранён.
     * \param error Причина отказа на английском (через tr() вызывающего); может быть `nullptr`.
     * \return `false`, если запись не разобрана или адресов больше kMaxTargets.
     *
     * Адреса сети и широковещательный для префиксов до /30 не включаются: на них никто не
     * отвечает как на хост.
     */
    static bool parseRange(const QString &text, QList<QHostAddress> *targets, QString *error);

    /// \brief Одна сеть этого компьютера.
    struct LocalNetwork
    {
        QString cidr;          ///< `10.0.0.0/24`.
        QString interfaceName; ///< `en0`, `eth0`.
    };

    /**
     * \brief Сети, в которых сейчас находится этот компьютер, — для выбора диапазона поиска.
     *
     * Работающие интерфейсы с IPv4, кроме петли и туннелей «точка–точка» (VPN). Сеть шире /22
     * сужается до /24 вокруг собственного адреса: перебирать корпоративную /16 по умолчанию
     * никто не просил.
     */
    static QList<LocalNetwork> localNetworks();

    /**
     * \brief Адреса сетей этого компьютера.
     *
     * Работающие интерфейсы с IPv4, кроме петли и туннелей «точка–точка» (VPN). Сеть шире /22
     * сужается до /24 вокруг собственного адреса: перебирать корпоративную /16 по умолчанию
     * никто не просил.
     */
    static QList<QHostAddress> localTargets();

    /// \brief Начать проход. Предыдущий, если шёл, прерывается, результаты сбрасываются.
    void start(const Options &options);

    /// \brief Прервать проход. Найденное к этому моменту остаётся.
    void cancel();

    bool isRunning() const { return m_running; }

    /// \brief Сколько адресов в проходе.
    int targetCount() const { return m_targetCount; }

    /// \brief Сколько адресов уже получили ответ по первому порту.
    int hostsDone() const { return m_hostsDone; }

    /// \brief Сколько хостов отброшено как «открытые на всём» (перехват роутером, VPN или прокси).
    int interceptedCount() const { return int(m_intercepted.size()); }

    /// \brief Сколько соединений сейчас в полёте; не больше Options::concurrency.
    int activeProbes() const { return int(m_sockets.size()); }

    /// \brief Найденные хосты по возрастанию адреса.
    QList<Host> results() const;

    /**
     * \brief Похоже ли, что порт «открыт» не на хостах, а на посреднике.
     * \param found Сколько хостов ответило.
     * \param targets Сколько адресов опрашивалось.
     *
     * VPN-клиент, прозрачный прокси или шлюз с перехватом принимают соединение за любой
     * адрес, и результат поиска превращается в «нашлось всё». Отличить это от сети, где
     * действительно всё занято, нельзя, но ответивших больше половины адресов (при
     * заметном их числе) — достаточный повод предупредить, а не выдавать список за правду.
     */
    static bool looksIntercepted(int found, int targets) { return found >= 8 && found * 2 > targets; }

Q_SIGNALS:
    /// \brief Изменились результаты или ход прохода.
    void changed();

    /// \brief Проход закончен (в том числе прерванный).
    void finished();

private:
    struct Probe
    {
        QHostAddress address;
        int port = 0;
        bool control = false; ///< Контрольная проба (см. Options::controlPorts), а не поиск.
    };

    /// \brief Запустить столько соединений, сколько позволяет предел.
    void pump();

    /// \brief Начать одно соединение.
    void launch(const Probe &probe);

    /// \brief Соединение завершено: открыт порт или нет.
    void complete(QTcpSocket *socket, const Probe &probe, bool open, int generation);

    /// \brief Хост прошёл контрольные пробы: теперь он найденный, и у него ищутся следующие порты.
    void confirmHost(const QString &key);

    /// \brief Поставить в начало очереди следующий порт хоста, если предел ещё не достигнут.
    void queueNextPort(const QHostAddress &address, int port);

    /// \brief Проход исчерпан: очередь пуста и ничего не в полёте.
    void finishIfIdle();

    Options m_options;
    std::deque<Probe> m_queue;
    QList<QTcpSocket *> m_sockets; ///< Соединения в полёте — чтобы прервать их при cancel().
    QHash<QString, Host> m_hosts;  ///< Найденное и проверенное, по строке адреса.

    /// \brief Хосты, у которых открылся первый порт и которые ждут контрольных проб.
    QHash<QString, Host> m_candidates;
    QHash<QString, int> m_controlsPending; ///< Сколько контрольных проб ещё не вернулось, по хосту.
    QSet<QString> m_intercepted;           ///< Отброшенные как «открытые на всём».
    QList<int> m_controlPorts;             ///< Контрольные порты текущего прохода.

    int m_targetCount = 0;
    int m_hostsDone = 0;
    bool m_running = false;

    /// \brief Номер прохода: запоздалые ответы прерванного не должны попасть в новый.
    int m_generation = 0;
};

} // namespace spotty
