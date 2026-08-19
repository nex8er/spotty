/**
 * \file PcanLibrary.cpp
 * \brief Реализация spotty::PcanLibrary.
 *
 * Объявления ниже соответствуют `PCANBasic.h` (PEAK) и `PCBUSB.h` (UV Software): это FFI,
 * и раскладка обязана совпадать побайтово. Собственный заголовок здесь потому, что ни
 * того, ни другого файла в поставке Spotty нет — библиотека грузится в рантайме.
 */
#include "PcanLibrary.h"

#include <QLoggingCategory>

#include <cstring>
#include <iterator>

namespace spotty {

namespace {

Q_LOGGING_CATEGORY(lcPcan, "spotty.plugins.clican")

using TPCANHandle = quint16;
using TPCANStatus = quint32;

/// \brief Раскладка `TPCANMsg`: 11/29-битный идентификатор, тип, длина и восемь байт.
struct RawMsg
{
    quint32 ID;
    quint8 MSGTYPE;
    quint8 LEN;
    quint8 DATA[8];
};

/// \brief Раскладка `TPCANTimestamp`. Нам не нужна (метку ставим сами, монотонными
///        часами), но CAN_Read() требует указатель — передавать nullptr документация не
///        разрешает.
struct RawTimestamp
{
    quint32 millis;
    quint16 millis_overflow;
    quint16 micros;
};

using FnInitialize = TPCANStatus (*)(TPCANHandle, quint16, quint8, quint32, quint16);
using FnUninitialize = TPCANStatus (*)(TPCANHandle);
using FnRead = TPCANStatus (*)(TPCANHandle, RawMsg *, RawTimestamp *);
using FnWrite = TPCANStatus (*)(TPCANHandle, RawMsg *);
using FnGetValue = TPCANStatus (*)(TPCANHandle, quint8, void *, quint32);
using FnSetValue = TPCANStatus (*)(TPCANHandle, quint8, void *, quint32);
using FnGetErrorText = TPCANStatus (*)(TPCANStatus, quint16, char *);

constexpr TPCANStatus kOk = 0x00000u;
constexpr TPCANStatus kQueueEmpty = 0x00020u;
/// \brief «Операция выполнена, но с оговорками» — для нас это успех.
constexpr TPCANStatus kCaution = 0x2000000u;

constexpr quint8 kParamDeviceId = 0x01u;
constexpr quint8 kParamReceiveEvent = 0x03u;
constexpr quint8 kParamApiVersion = 0x05u;
constexpr quint8 kParamChannelCondition = 0x0Du;
constexpr quint8 kParamHardwareName = 0x0Eu;

constexpr quint32 kConditionAvailable = 0x01u;
constexpr quint32 kConditionOccupied = 0x02u;

constexpr quint8 kMsgStandard = 0x00u;
constexpr quint8 kMsgRtr = 0x01u;
constexpr quint8 kMsgExtended = 0x02u;

/// \brief Служебные типы кадров: статус шины, кадр ошибки, эхо собственной посылки.
constexpr quint8 kMsgServiceMask = 0x20u /* ECHO */ | 0x40u /* ERRFRAME */ | 0x80u /* STATUS */;

constexpr TPCANHandle kNoneBus = 0x00u;

/// \brief Скорость шины и соответствующий ей код регистров SJA1000.
struct BitrateEntry
{
    int bitsPerSecond;
    quint16 code;
};

/// \brief Таблица из PCAN-Basic; классический CAN другого способа задать скорость не даёт.
constexpr BitrateEntry kBitrates[] = {
    {1'000'000, 0x0014}, {800'000, 0x0016}, {500'000, 0x001C}, {250'000, 0x011C},
    {125'000, 0x031C},   {100'000, 0x432F}, {83'333, 0x852B},  {50'000, 0x472F},
    {33'333, 0x8B2F},    {20'000, 0x532F},  {10'000, 0x672F},  {5'000, 0x7F7F},
};

} // namespace

PcanLibrary &PcanLibrary::instance()
{
    static PcanLibrary library;
    return library;
}

bool PcanLibrary::load()
{
    if (m_loadAttempted)
        return m_loaded;
    m_loadAttempted = true;

    // На macOS библиотека UV Software ставится в /usr/local/lib, куда динамический
    // загрузчик сам не смотрит: голого имени недостаточно, нужен полный путь. Остальные
    // имена — на случай собственной установки пользователя.
    const QStringList candidates = {
#if defined(Q_OS_MACOS)
        QStringLiteral("/usr/local/lib/libPCBUSB.dylib"),
        QStringLiteral("/opt/homebrew/lib/libPCBUSB.dylib"),
        QStringLiteral("PCBUSB"),
#elif defined(Q_OS_WIN)
        QStringLiteral("PCANBasic"),
#else
        QStringLiteral("pcanbasic"),
        QStringLiteral("libpcanbasic.so.4"),
#endif
    };

    for (const QString &candidate : candidates) {
        m_library.setFileName(candidate);
        if (m_library.load()) {
            qCInfo(lcPcan) << "PCAN-Basic loaded from" << m_library.fileName();
            break;
        }
    }

    if (!m_library.isLoaded()) {
        qCInfo(lcPcan) << "PCAN-Basic is not installed; the CLI-CAN plugin stays idle";
        return false;
    }

    m_initialize = reinterpret_cast<void *>(m_library.resolve("CAN_Initialize"));
    m_uninitialize = reinterpret_cast<void *>(m_library.resolve("CAN_Uninitialize"));
    m_read = reinterpret_cast<void *>(m_library.resolve("CAN_Read"));
    m_write = reinterpret_cast<void *>(m_library.resolve("CAN_Write"));
    m_getValue = reinterpret_cast<void *>(m_library.resolve("CAN_GetValue"));
    m_setValue = reinterpret_cast<void *>(m_library.resolve("CAN_SetValue"));
    m_getErrorText = reinterpret_cast<void *>(m_library.resolve("CAN_GetErrorText"));

    m_loaded = m_initialize && m_uninitialize && m_read && m_write && m_getValue
        && m_setValue && m_getErrorText;
    if (!m_loaded)
        qCWarning(lcPcan) << "PCAN-Basic found but does not export the expected symbols";

    return m_loaded;
}

bool PcanLibrary::isAvailable()
{
    return load();
}

QString PcanLibrary::apiVersion()
{
    if (!load())
        return {};

    char buffer[256] = {};
    const auto getValue = reinterpret_cast<FnGetValue>(m_getValue);
    if (getValue(kNoneBus, kParamApiVersion, buffer, sizeof(buffer)) != kOk)
        return {};
    return QString::fromLatin1(buffer);
}

QList<PcanLibrary::ChannelInfo> PcanLibrary::availableChannels()
{
    QList<ChannelInfo> result;
    if (!load())
        return result;

    const auto getValue = reinterpret_cast<FnGetValue>(m_getValue);

    // Перебор ручек, а не PCAN_ATTACHED_CHANNELS: последний не поддержан PCBUSB, а
    // ветвиться по платформам там, где один способ работает везде, значило бы держать
    // непроверяемый на этой машине код. Опрос восьми ручек стоит десятки микросекунд.
    for (int index = 1; index <= kMaxUsbChannel; ++index) {
        const Handle handle = usbBusHandle(index);

        quint32 condition = 0;
        if (getValue(handle, kParamChannelCondition, &condition, sizeof(condition)) != kOk)
            continue;
        if ((condition & (kConditionAvailable | kConditionOccupied)) == 0)
            continue;

        ChannelInfo info;
        info.handle = handle;
        info.index = index;
        info.occupied = (condition & kConditionOccupied) != 0;

        char name[64] = {};
        if (getValue(handle, kParamHardwareName, name, sizeof(name)) == kOk)
            info.name = QString::fromLatin1(name);
        if (info.name.isEmpty())
            info.name = QStringLiteral("PCAN-USB");

        quint32 deviceId = 0xFFFFFFFFu;
        if (getValue(handle, kParamDeviceId, &deviceId, sizeof(deviceId)) == kOk)
            info.deviceId = deviceId;

        result.append(info);
    }

    return result;
}

bool PcanLibrary::initialize(Handle handle, quint16 btr0btr1, QString *error)
{
    if (!load()) {
        if (error)
            *error = QStringLiteral("PCAN-Basic library is not installed");
        return false;
    }

    const auto initializeFn = reinterpret_cast<FnInitialize>(m_initialize);
    const TPCANStatus status = initializeFn(handle, btr0btr1, 0, 0, 0);
    if (status != kOk && status != kCaution) {
        if (error)
            *error = errorText(status);
        return false;
    }
    return true;
}

void PcanLibrary::uninitialize(Handle handle)
{
    if (!m_loaded)
        return;
    reinterpret_cast<FnUninitialize>(m_uninitialize)(handle);
}

bool PcanLibrary::read(Handle handle, Frame *frame, bool *empty, QString *error)
{
    if (empty)
        *empty = false;
    if (!m_loaded || !frame)
        return false;

    RawMsg message = {};
    RawTimestamp timestamp = {};
    const TPCANStatus status =
        reinterpret_cast<FnRead>(m_read)(handle, &message, &timestamp);

    if (status == kQueueEmpty) {
        if (empty)
            *empty = true;
        return false;
    }
    if (status != kOk && status != kCaution) {
        if (error)
            *error = errorText(status);
        return false;
    }

    // Кадры статуса и ошибок приходят по тому же каналу, что и данные, и отличаются только
    // типом. Пропустить их дальше значило бы выдать содержимое служебной структуры за
    // байты из CLI платы.
    if ((message.MSGTYPE & kMsgServiceMask) != 0 || (message.MSGTYPE & kMsgRtr) != 0) {
        if (empty)
            *empty = true;
        return false;
    }

    frame->id = message.ID;
    frame->extended = (message.MSGTYPE & kMsgExtended) != 0;
    frame->data = QByteArray(reinterpret_cast<const char *>(message.DATA),
                             qMin<int>(message.LEN, 8));
    return true;
}

bool PcanLibrary::write(Handle handle, quint32 id, const QByteArray &payload, QString *error)
{
    if (!m_loaded)
        return false;

    RawMsg message = {};
    message.ID = id;
    message.MSGTYPE = kMsgStandard;
    message.LEN = static_cast<quint8>(qMin<int>(payload.size(), 8));
    std::memcpy(message.DATA, payload.constData(), message.LEN);

    const TPCANStatus status = reinterpret_cast<FnWrite>(m_write)(handle, &message);
    if (status != kOk && status != kCaution) {
        if (error)
            *error = errorText(status);
        return false;
    }
    return true;
}

qintptr PcanLibrary::receiveEvent(Handle handle)
{
    if (!m_loaded)
        return -1;

    // Размер запроса важен: на POSIX драйвер отдаёт int, на Windows — HANDLE. Просить
    // sizeof(qintptr) на POSIX значило бы позволить драйверу писать восемь байт туда, где
    // он пишет четыре, и читать мусор в старшей половине.
#if defined(Q_OS_WIN)
    void *handleValue = nullptr;
    if (reinterpret_cast<FnGetValue>(m_getValue)(handle, kParamReceiveEvent, &handleValue,
                                                 sizeof(handleValue))
        != kOk) {
        return -1;
    }
    return reinterpret_cast<qintptr>(handleValue);
#else
    int fd = -1;
    if (reinterpret_cast<FnGetValue>(m_getValue)(handle, kParamReceiveEvent, &fd, sizeof(fd))
        != kOk) {
        return -1;
    }
    return fd;
#endif
}

bool PcanLibrary::setReceiveEvent(Handle handle, qintptr event)
{
    if (!m_loaded)
        return false;

#if defined(Q_OS_WIN)
    void *handleValue = reinterpret_cast<void *>(event);
    return reinterpret_cast<FnSetValue>(m_setValue)(handle, kParamReceiveEvent, &handleValue,
                                                    sizeof(handleValue))
        == kOk;
#else
    Q_UNUSED(handle);
    Q_UNUSED(event);
    return false; // На POSIX событие создаёт драйвер, а не приложение.
#endif
}

QString PcanLibrary::errorText(quint32 status)
{
    if (!m_loaded)
        return QStringLiteral("0x%1").arg(status, 0, 16);

    char buffer[256] = {};
    // 0 — нейтральный английский; язык интерфейса тут ни при чём, текст технический и
    // попадает пользователю уже внутри переведённой рамки вида «Не удалось открыть: %1».
    if (reinterpret_cast<FnGetErrorText>(m_getErrorText)(status, 0, buffer) != kOk)
        return QStringLiteral("0x%1").arg(status, 0, 16);
    return QString::fromLatin1(buffer).trimmed();
}

quint16 PcanLibrary::btrCode(int bitsPerSecond)
{
    for (const BitrateEntry &entry : kBitrates) {
        if (entry.bitsPerSecond == bitsPerSecond)
            return entry.code;
    }
    return 0;
}

QList<int> PcanLibrary::supportedBitrates()
{
    QList<int> result;
    result.reserve(std::size(kBitrates));
    for (const BitrateEntry &entry : kBitrates)
        result.append(entry.bitsPerSecond);
    return result;
}

} // namespace spotty
