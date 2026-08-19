/**
 * \file PcanLibrary.h
 * \brief Тонкая обёртка над PCAN-Basic (PCBUSB на macOS), загружаемой в рантайме.
 */
#pragma once

#include <QByteArray>
#include <QLibrary>
#include <QList>
#include <QString>

namespace spotty {

/**
 * \class PcanLibrary
 * \brief Единственная точка входа в PCAN-Basic для всего плагина.
 *
 * \par Почему рантайм, а не время сборки
 *
 * Драйвер PEAK — сторонняя, отдельно устанавливаемая вещь: `PCANBasic.dll` из пакета
 * PEAK на Windows, `libpcanbasic.so` на Linux, `libPCBUSB.dylib` от UV Software на macOS.
 * Линковка на этапе сборки сделала бы сборку Spotty зависимой от того, чего на машине
 * может не быть вовсе. Как и у J-Link (см. spotty::JlinkArmLibrary), библиотека грузится
 * через QLibrary при первом обращении; без неё enumerate() плагина возвращает пустой
 * список, и это не ошибка.
 *
 * \par Три реализации одного API
 *
 * PCBUSB намеренно повторяет API PEAK, поэтому один набор сигнатур обслуживает все три
 * платформы. Расхождения есть, и они учтены здесь:
 * - `PCAN_ATTACHED_CHANNELS` на macOS не поддержан, поэтому каналы ищутся перебором
 *   восьми ручек через `PCAN_CHANNEL_CONDITION` — способ, работающий везде (замерено:
 *   восемь опросов занимают 0.03 мс, что позволяет звать enumerate() хоть каждую секунду);
 * - `PCAN_RECEIVE_EVENT` на POSIX **возвращает** файловый дескриптор для select(), а на
 *   Windows принимает созданный приложением HANDLE. Обе ветки живут в spotty::CanBus.
 *
 * \note Объявления типов ниже — не заголовок PEAK (его нет в поставке Spotty), а
 *       минимальный набор, побайтово совпадающий с `PCANBasic.h` и `PCBUSB.h`. Обычное
 *       выравнивание здесь **правильное**: в отличие от структур SEGGER, все поля
 *       TPCANMsg кратны своему размеру и никаких указателей в них нет, поэтому pack(1) и
 *       естественная раскладка дают одно и то же (проверено на macOS/arm64 отдельной
 *       программой на dlopen() в обход Spotty).
 */
class PcanLibrary
{
public:
    /// \return Единственный экземпляр на процесс.
    static PcanLibrary &instance();

    /// \brief Ручка канала PCAN: `PCAN_USBBUS1` == 0x51 и далее.
    using Handle = quint16;

    /// \struct ChannelInfo
    /// \brief Один найденный в системе канал PCAN.
    struct ChannelInfo
    {
        Handle handle = 0;  ///< Ручка для CAN_Initialize().
        int index = 0;      ///< Номер канала в терминах PCAN: 1..16.
        QString name;       ///< Имя железа, как его сообщает драйвер: «PCAN-USB».
        quint32 deviceId = 0xFFFFFFFFu; ///< Номер устройства, назначаемый пользователем.
        bool occupied = false;          ///< Канал уже занят другим приложением.

        /// \return `true`, если #deviceId задан человеком, а не оставлен по умолчанию.
        ///
        /// Незаданный номер драйверы показывают как 0xFF (PCBUSB) или 0xFFFFFFFF (PEAK) —
        /// оба значения означают «различить два одинаковых адаптера нечем».
        bool hasDeviceId() const { return deviceId != 0xFFu && deviceId != 0xFFFFFFFFu; }
    };

    /// \struct Frame
    /// \brief Один кадр CAN с отметкой времени приёма.
    struct Frame
    {
        quint32 id = 0;      ///< Идентификатор (11 бит для туннеля CLI).
        QByteArray data;     ///< Полезная нагрузка, 0..8 байт.
        bool extended = false; ///< Кадр с 29-битным идентификатором.
    };

    /// \return `true`, если библиотека найдена и все нужные символы разрешились.
    bool isAvailable();

    /// \brief Версия PCAN-Basic API строкой, например «4.9.0.1». Пустая, если нет библиотеки.
    QString apiVersion();

    /**
     * \brief Каналы, видимые в системе прямо сейчас.
     *
     * Ничего не открывает и не мешает уже открытому каналу: чистый опрос состояния.
     */
    QList<ChannelInfo> availableChannels();

    /**
     * \brief Открыть канал.
     * \param handle Ручка канала.
     * \param btr0btr1 Код скорости (см. btrCode()).
     * \param error Техническое описание причины отказа, непереведённое.
     */
    bool initialize(Handle handle, quint16 btr0btr1, QString *error);

    /// \brief Закрыть канал. Безопасно при уже закрытом.
    void uninitialize(Handle handle);

    /**
     * \brief Прочитать один кадр из очереди приёма.
     * \param handle Ручка канала.
     * \param frame Куда положить кадр.
     * \param empty Признак «очередь пуста» — это не ошибка, а обычный ответ.
     * \return `false` при ошибке либо при пустой очереди (тогда \p empty истинно).
     *
     * Служебные кадры (статус, ошибка, эхо) отбрасываются: туннелю они не нужны, а
     * отличить их от данных умеет только тот, кто видит поле типа.
     */
    bool read(Handle handle, Frame *frame, bool *empty, QString *error);

    /// \brief Отправить кадр со стандартным (11-битным) идентификатором.
    bool write(Handle handle, quint32 id, const QByteArray &payload, QString *error);

    /**
     * \brief Файловый дескриптор (POSIX) или HANDLE (Windows) для ожидания приёма.
     * \return -1, если библиотека этого не умеет — тогда остаётся опрос.
     *
     * На POSIX значение возвращает сам драйвер, на Windows — наоборот, приложение обязано
     * создать событие и отдать его драйверу; ветвление живёт в spotty::CanBus, сюда
     * приходит уже готовое значение.
     */
    qintptr receiveEvent(Handle handle);

    /// \brief Передать драйверу событие приёма (нужно только на Windows).
    bool setReceiveEvent(Handle handle, qintptr event);

    /// \brief Человекочитаемый текст кода состояния PCAN.
    QString errorText(quint32 status);

    /**
     * \brief Код BTR0/BTR1 для скорости в битах в секунду.
     * \return 0, если такой скорости в таблице контроллера нет.
     *
     * Таблица — единственный способ задать скорость в классическом PCAN-Basic: драйвер
     * принимает не число бит в секунду, а пару регистров тайминга SJA1000.
     */
    static quint16 btrCode(int bitsPerSecond);

    /// \brief Скорости, которые умеет задать btrCode(), от быстрой к медленной.
    static QList<int> supportedBitrates();

    /// \brief Наибольший номер канала USB, который поддерживают все три реализации.
    static constexpr int kMaxUsbChannel = 8;

    /**
     * \brief Ручка канала PCAN-USB по его номеру.
     * \param index Номер канала, 1..#kMaxUsbChannel.
     * \return 0, если номер вне диапазона.
     */
    static constexpr Handle usbBusHandle(int index)
    {
        // PCAN_USBBUS1..PCAN_USBBUS8 идут подряд от 0x51 — так их задаёт PCAN-Basic.
        return (index >= 1 && index <= kMaxUsbChannel) ? static_cast<Handle>(0x51 + index - 1)
                                                       : Handle(0);
    }

private:
    PcanLibrary() = default;

    /// \brief Найти и открыть библиотеку. Повторные вызовы бесплатны.
    bool load();

    QLibrary m_library;
    bool m_loadAttempted = false;
    bool m_loaded = false;

    void *m_initialize = nullptr;
    void *m_uninitialize = nullptr;
    void *m_read = nullptr;
    void *m_write = nullptr;
    void *m_getValue = nullptr;
    void *m_setValue = nullptr;
    void *m_getErrorText = nullptr;
};

} // namespace spotty
