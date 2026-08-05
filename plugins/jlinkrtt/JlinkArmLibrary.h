/**
 * \file JlinkArmLibrary.h
 * \brief Тонкая обёртка над libjlinkarm (SEGGER J-Link SDK), загружаемой в рантайме.
 */
#pragma once

#include <QByteArray>
#include <QLibrary>
#include <QList>
#include <QMutex>
#include <QString>

namespace spotty {

/**
 * \class JlinkArmLibrary
 * \brief Единственная точка входа в libjlinkarm для всего плагина.
 *
 * \par Почему рантайм, а не время сборки
 *
 * libjlinkarm — часть стороннего, отдельно устанавливаемого SEGGER J-Link software, а не
 * часть Spotty. Линковка на этапе сборки сделала бы сборку Spotty зависимой от того, что
 * на машине разработчика (и тем более пользователя) может не стоять вовсе. Библиотека
 * грузится через QLibrary при первом обращении; если её нет, enumerate() плагина просто
 * возвращает пустой список — как UART без Qt6::SerialPort.
 *
 * \par Почему единственное соединение на процесс
 *
 * Классический C API J-Link SDK — не объект с независимыми хендлами, а глобальное для
 * процесса состояние: `JLINK_Open()`/`JLINK_Close()` открывают и закрывают «текущий»
 * выбранный эмулятор. Два одновременных подключения из одного процесса эта версия API не
 * поддерживает. Класс отражает это ограничение явно: openConnection() отказывает, если
 * соединение уже открыто, вместо того чтобы тихо испортить состояние второго канала.
 * Несколько RTT-каналов **одного** зонда через одно соединение работают: RTTERMINAL-вызовы
 * принимают номер буфера отдельным параметром.
 *
 * \note Методы синхронные и могут занимать заметное время (JLINK_Connect() — до нескольких
 *       секунд). Вызывать их следует из потока ввода-вывода канала, не из потока UI.
 */
class JlinkArmLibrary
{
public:
    /// \return Единственный экземпляр на процесс.
    static JlinkArmLibrary &instance();

    /// \struct ProbeInfo
    /// \brief Один найденный на USB зонд J-Link.
    struct ProbeInfo
    {
        quint32 serialNumber = 0; ///< Серийный номер — устойчивый идентификатор зонда.
        QString product;          ///< Имя модели, как его сообщает сам зонд, например «J-Link».
    };

    /// \brief Целевой интерфейс отладки.
    enum TargetInterface { Swd, Jtag };

    /**
     * \brief Найти зонды J-Link, подключённые по USB.
     * \return Пустой список, если библиотека не найдена или зондов нет — не ошибка.
     *
     * Ничего не открывает и не мешает уже установленному соединению: чистый опрос USB,
     * безопасный для вызова хоть каждую секунду.
     */
    QList<ProbeInfo> enumerateProbes();

    /**
     * \brief Открыть соединение с зондом.
     * \param serialNumber Серийный номер зонда из enumerateProbes().
     * \param error Техническое (непереведённое) описание причины отказа.
     * \return `true` при успехе.
     */
    bool openConnection(quint32 serialNumber, QString *error);

    /// \brief Закрыть соединение. Безопасно вызывать, даже если оно не было открыто.
    void closeConnection();

    /// \return `true`, если соединение сейчас открыто этим процессом.
    bool isConnectionOpen() const;

    /**
     * \brief Выбрать интерфейс отладки и скорость.
     * \param targetInterface SWD или JTAG.
     * \param speedKhz Скорость в кГц.
     *
     * Требует уже открытого соединения. Сама по себе не может достоверно сообщить об
     * ошибке — реальная проверка происходит в connectTarget() через код возврата
     * `JLINK_Connect()`.
     */
    bool selectInterfaceAndSpeed(TargetInterface targetInterface, int speedKhz);

    /**
     * \brief Подключиться к целевому чипу.
     * \param deviceName Точное имя устройства из базы SEGGER; пустая строка пропускает шаг
     *        выбора устройства и полагается на то, что уже выбрано в J-Link software.
     * \param error Техническое описание причины отказа.
     */
    bool connectTarget(const QString &deviceName, QString *error);

    /// \brief Запустить RTT-терминал с автопоиском управляющего блока в памяти таргета.
    bool rttStart(QString *error);

    /// \brief Остановить RTT-терминал. Безопасно вызывать, даже если он не был запущен.
    void rttStop();

    /**
     * \brief Прочитать накопленные данные одного канала RTT.
     * \param channel Номер буфера (0 — обычно «Terminal»).
     * \param maxBytes Сколько байт читать за один вызов.
     * \param running Если не `nullptr`, получает признак того, что управляющий блок RTT
     *        на таргете уже найден. Пока он не найден, отсутствие данных — это ожидаемо,
     *        а не ошибка: автопоиск идёт в фоне на стороне библиотеки.
     * \return Прочитанные байты; пустой массив, если данных нет.
     */
    QByteArray rttRead(int channel, int maxBytes, bool *running);

    /**
     * \brief Записать данные в один канал RTT.
     * \param channel Номер буфера.
     * \param data Данные для отправки.
     * \return Число принятых байт или -1 при ошибке.
     */
    qint64 rttWrite(int channel, const QByteArray &data);

private:
    JlinkArmLibrary() = default;

    /// \brief Загрузить библиотеку и разрешить нужные символы. Идемпотентен.
    bool load();

    QLibrary m_library;
    bool m_loadAttempted = false;
    bool m_loaded = false;
    bool m_connectionOpen = false;

    /// \brief Защищает состояние соединения от одновременных вызовов из разных каналов.
    mutable QMutex m_mutex;

    // Указатели на функции библиотеки; конкретные типы сигнатур объявлены в .cpp — наружу
    // они не нужны. QFunctionPointer — то, что возвращает QLibrary::resolve().
    QFunctionPointer m_fnEmuGetList = nullptr;
    QFunctionPointer m_fnEmuSelectByUsbSn = nullptr;
    QFunctionPointer m_fnOpenEx = nullptr;
    QFunctionPointer m_fnClose = nullptr;
    QFunctionPointer m_fnTifSelect = nullptr;
    QFunctionPointer m_fnSetSpeed = nullptr;
    QFunctionPointer m_fnConnect = nullptr;
    QFunctionPointer m_fnExecCommand = nullptr;
    QFunctionPointer m_fnRttControl = nullptr;
    QFunctionPointer m_fnRttRead = nullptr;
    QFunctionPointer m_fnRttWrite = nullptr;
};

} // namespace spotty
