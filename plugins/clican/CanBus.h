/**
 * \file CanBus.h
 * \brief Один открытый канал PCAN: поток чтения, отправка, учёт отозвавшихся узлов.
 */
#pragma once

#include "CliCanProtocol.h"
#include "PcanLibrary.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QMutex>
#include <QString>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <thread>

namespace spotty {

/**
 * \class CanBus
 * \brief Физическая шина CAN, разделяемая всеми, кому она нужна одновременно.
 *
 * \par Почему не QObject
 *
 * Шину заводит то диалог настроек (поток UI, ищет узлы), то открывающийся канал (поток
 * ввода-вывода) — кто первым попросил. У QObject есть привязка к потоку, и объект,
 * созданный в потоке ввода-вывода одной сессии, пришлось бы как-то безопасно удалять
 * после того, как этот поток закончился. Обычный класс такой задачи не ставит: получатели
 * кадров подписываются функцией, а перескок в свой поток делает каждый сам —
 * spotty::CliCanChannel через QMetaObject::invokeMethod(), диалог не делает вовсе, потому
 * что читает готовый список под мьютексом.
 *
 * \par Потоки
 *
 * Приёмом занимается собственный поток. Он не опрашивает шину в цикле, а спит на событии
 * драйвера (`select()` на POSIX, `WaitForSingleObject()` на Windows); опрос остаётся
 * запасным путём для сборок PCAN-Basic, которые событие не отдают. Отметка времени
 * ставится **в момент чтения** и монотонными часами: из неё ядро считает межбайтовые
 * паузы для пакетизации.
 *
 * \warning Обработчики кадров вызываются из потока приёма, и пока идёт вызов, держится
 *          мьютекс подписки. Из обработчика нельзя блокировать шину — снимать подписку,
 *          закрывать канал, ждать другой поток.
 */
class CanBus
{
public:
    /// \brief Обработчик принятого кадра: идентификатор, данные, монотонное время приёма.
    using FrameHandler = std::function<void(quint32, const QByteArray &, qint64)>;

    /// \brief Обработчик ошибки шины; текст уже пригоден к показу пользователю.
    using ErrorHandler = std::function<void(const QString &)>;

    /// \param handle Ручка канала PCAN.
    explicit CanBus(PcanLibrary::Handle handle);
    ~CanBus();

    CanBus(const CanBus &) = delete;
    CanBus &operator=(const CanBus &) = delete;

    /**
     * \brief Открыть шину на заданной скорости.
     * \param bitrate Скорость в битах в секунду; должна быть в PcanLibrary::supportedBitrates().
     * \param error Сообщение для пользователя.
     *
     * Повторный вызов с той же скоростью ничего не делает. С другой — закрывает и
     * открывает шину заново: у канала PCAN одна скорость на всех, кто им пользуется.
     */
    bool open(int bitrate, QString *error);

    /// \brief Закрыть шину и остановить поток приёма. Безопасно при уже закрытой.
    void close();

    /// \return `true`, если шина сейчас открыта.
    bool isOpen() const { return m_open; }

    /// \return Скорость, на которой шина открыта, в битах в секунду.
    int bitrate() const { return m_bitrate; }

    /// \brief Ручка канала PCAN.
    PcanLibrary::Handle handle() const { return m_handle; }

    /**
     * \brief Отправить кадр.
     * \return `true` при успехе. Вызывается из любого потока.
     */
    bool send(quint32 id, const QByteArray &payload, QString *error);

    /**
     * \brief Включить широковещательный опрос присутствия.
     *
     * Пока включён, поток приёма раз в ::kDiscoveryIntervalMs шлёт пустой пакет с
     * идентификатором 0x400, на который откликается каждый узел. Выключается, когда
     * диалог настроек закрыт: лишний трафик на рабочей шине никому не нужен.
     */
    void setDiscoveryEnabled(bool enabled);

    /**
     * \brief Узлы, отозвавшиеся за последние ::kNodeExpiryMs.
     *
     * Снимок под мьютексом; звать можно из любого потока, в том числе из потока UI.
     */
    QList<int> nodes() const;

    /// \brief Подписаться на кадры. Возвращает метку для removeHandler().
    int addHandler(FrameHandler frameHandler, ErrorHandler errorHandler);

    /**
     * \brief Отписаться.
     *
     * Возвращает управление, только когда обработчик заведомо не выполняется: подписчику
     * после этого можно спокойно умирать.
     */
    void removeHandler(int token);

    /// \brief Как часто слать широковещательный запрос присутствия.
    static constexpr int kDiscoveryIntervalMs = 500;

    /// \brief Сколько узел числится на шине после последнего ответа.
    static constexpr qint64 kNodeExpiryMs = 5000;

private:
    /// \brief Тело потока приёма.
    void readLoop();

    /// \brief Дождаться данных: событие драйвера, а при его отсутствии — короткий сон.
    void waitForFrames();

    /// \brief Забрать из очереди всё накопившееся и раздать подписчикам.
    void drainQueue();

    /// \brief Раздать один кадр подписчикам, обновив попутно список узлов.
    void dispatch(quint32 id, const QByteArray &data, qint64 monotonicNs);

    /// \brief Сообщить подписчикам об ошибке шины.
    void reportError(const QString &message);

    PcanLibrary::Handle m_handle;
    int m_bitrate = 0;
    std::atomic<bool> m_open{false};
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_discovery{false};

    std::thread m_reader;
    qintptr m_receiveEvent = -1;
    /// \brief Событие приёма создано нами (Windows) — значит, нам его и закрывать.
    bool m_ownsReceiveEvent = false;

    QElapsedTimer m_clock; ///< Монотонные часы для меток времени принятых кадров.

    mutable QMutex m_txMutex;      ///< Отправка из разных потоков идёт по одному кадру.
    mutable QMutex m_nodesMutex;   ///< Защищает #m_nodes от чтения из потока UI.
    mutable QMutex m_handlerMutex; ///< Держится на время вызова обработчиков.

    clican::NodeDirectory m_nodes;

    /// \struct Subscriber
    /// \brief Один подписчик на кадры шины.
    struct Subscriber
    {
        int token = 0;
        FrameHandler onFrame;
        ErrorHandler onError;
    };

    QList<Subscriber> m_handlers;
    int m_nextToken = 1;
};

/**
 * \class CanBusPool
 * \brief Общие шины по ручкам каналов: одна открытая шина на канал PCAN.
 *
 * \par Зачем
 *
 * Драйвер отдаёт канал одному владельцу, а желающих двое: диалог настроек ищет узлы, а
 * открытый канал ведёт туннель, причём одновременно — узлы должны продолжать появляться в
 * списке и при открытом соединении. Пул отдаёт обоим один и тот же объект и закрывает
 * шину, когда её отпустил последний.
 *
 * \note Скорость задаёт **последний** попросивший: пользователь, поменявший её в диалоге,
 *       ожидает, что она поменялась, а не что его правку молча проигнорировали из-за уже
 *       открытого соединения. Ядро в этот момент всё равно переоткрывает канал —
 *       applySettings() у этого транспорта не переопределён.
 *
 * \par Почему не синглтон
 *
 * Раньше это был Meyer's singleton (`instance()`, локальная статическая переменная), и на
 * выгрузке плагина это уронило процесс: `QLibraryStore::cleanup()` вызывает
 * `~CliCanPlugin()` во время финализации статических объектов при `exit()`, и порядок
 * разрушения статика внутри самой выгружаемой библиотеки относительно этого вызова ничем
 * не гарантирован — `m_scanBus` пытался обратиться к уже недействительному `CanBusPool`,
 * `EXC_BAD_ACCESS` в `std::map::find()`. Пул теперь — обычный член `CliCanPlugin`,
 * разрушается вместе с ним по обычным правилам C++ (обратный порядок объявления полей), и
 * `CliCanChannel` получает указатель на него явно, через createChannel().
 */
class CanBusPool
{
public:
    /**
     * \brief Получить открытую шину канала.
     * \param handle Ручка канала PCAN.
     * \param bitrate Требуемая скорость в битах в секунду.
     * \param error Сообщение для пользователя при отказе.
     * \return Пустой указатель, если шину не удалось открыть.
     *
     * Шина закрывается сама, когда последний владелец отпустил указатель.
     */
    std::shared_ptr<CanBus> acquire(PcanLibrary::Handle handle, int bitrate, QString *error);

private:
    /// \brief Отпустить шину; закрывает её, когда ушёл последний владелец.
    void release(PcanLibrary::Handle handle);

    /**
     * \struct Entry
     * \brief Одна шина и число её нынешних владельцев.
     *
     * \par Почему счётчик свой, а не weak_ptr
     *
     * Очевидная схема «в пуле weak_ptr, наружу shared_ptr» здесь неверна, и неверна
     * незаметно. Между обнулением счётчика ссылок и работой удалителя объект ещё жив, а
     * weak_ptr::lock() уже возвращает пустоту: тот, кто в этот момент попросит шину,
     * создаст **вторую** на ту же ручку, откроет канал — и следом отработает удалитель
     * первой, закрыв только что открытый канал. Диалог настроек отпускает шину из потока
     * UI, канал берёт её из потока ввода-вывода, так что окно это не теоретическое.
     *
     * Со своим счётчиком создание, открытие, закрытие и разрушение идут под одним
     * мьютексом, и промежуточного состояния «уже не выдаётся, но ещё существует» просто
     * нет.
     */
    struct Entry
    {
        std::unique_ptr<CanBus> bus;
        int users = 0;
    };

    QMutex m_mutex;

    // std::map, а не QHash: QHash требует копируемое значение, а Entry владеет шиной через
    // unique_ptr и копироваться не должен — владелец у шины ровно один.
    std::map<PcanLibrary::Handle, Entry> m_entries;
};

} // namespace spotty
