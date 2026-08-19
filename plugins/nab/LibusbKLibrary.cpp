/**
 * \file LibusbKLibrary.cpp
 * \brief Реализация spotty::LibusbKLibrary.
 */
#include "LibusbKLibrary.h"

#include <QByteArray>
#include <QLoggingCategory>
#include <QScopeGuard>

#include <algorithm>
#include <array>
#include <cstring>

#include <windows.h>

namespace spotty {

namespace {

Q_LOGGING_CATEGORY(lcNab, "spotty.plugins.nab")

constexpr int kVendorId = 0x28E9;
constexpr int kProductId = 0x325A;
constexpr int kBulkPipeType = 2;
constexpr quint8 kEndpointDirectionIn = 0x80;
constexpr quint8 kUsbDescriptorDevice = 0x01;
constexpr quint8 kUsbDescriptorString = 0x03;
constexpr quint32 kPipeTransferTimeout = 3;

/**
 * \brief Открытая libusbK запись об устройстве.
 *
 * В структуре только поля, необходимые плагину. Её раскладка повторяет `KLST_DEVINFO`
 * из libusbk.h: получать эти данные через SetupAPI означало бы дублировать часть самого
 * драйвера и потерять его готовый устойчивый серийный номер.
 */
struct RawDeviceInfo
{
    struct CommonInfo
    {
        int vendorId;
        int productId;
        int interfaceNumber;
        char instanceId[256];
    } common;

    int driverId;
    char deviceInterfaceGuid[256];
    char deviceId[256];
    char classGuid[256];
    char manufacturer[256];
    char deviceDescription[256];
    char service[256];
    char symbolicLink[256];
    char devicePath[256];
    int libusb0FilterIndex;
    int connected;
    int syncFlags;
    int busNumber;
    int deviceAddress;
    char serialNumber[256];
};
static_assert(sizeof(RawDeviceInfo) == 2596);

#pragma pack(push, 1)
/// \brief Стандартный USB device descriptor, 18 байт по спецификации USB.
struct RawUsbDeviceDescriptor
{
    quint8 length;
    quint8 descriptorType;
    quint16 usbVersion;
    quint8 deviceClass;
    quint8 deviceSubClass;
    quint8 deviceProtocol;
    quint8 maxPacketSize0;
    quint16 vendorId;
    quint16 productId;
    quint16 deviceVersion;
    quint8 manufacturerIndex;
    quint8 productIndex;
    quint8 serialNumberIndex;
    quint8 configurationCount;
};

/// \brief Стандартный USB interface descriptor, 9 байт по спецификации USB.
struct RawUsbInterfaceDescriptor
{
    quint8 length;
    quint8 descriptorType;
    quint8 interfaceNumber;
    quint8 alternateSetting;
    quint8 endpointCount;
    quint8 interfaceClass;
    quint8 interfaceSubClass;
    quint8 interfaceProtocol;
    quint8 interfaceIndex;
};
#pragma pack(pop)
static_assert(sizeof(RawUsbDeviceDescriptor) == 18);
static_assert(sizeof(RawUsbInterfaceDescriptor) == 9);

/// \brief Раскладка WINUSB_PIPE_INFORMATION из winusb.h.
struct RawPipeInfo
{
    int type;
    quint8 address;
    quint16 maxPacketSize;
    quint8 interval;
};
static_assert(sizeof(RawPipeInfo) == 12);

using FnLstInit = BOOL(WINAPI *)(void **list, int flags);
using FnLstFree = BOOL(WINAPI *)(void *list);
using FnLstMoveNext = BOOL(WINAPI *)(void *list, RawDeviceInfo **info);
using FnUsbInit = BOOL(WINAPI *)(void **handle, RawDeviceInfo *info);
using FnUsbFree = BOOL(WINAPI *)(void *handle);
using FnUsbGetDescriptor = BOOL(WINAPI *)(void *handle, quint8 type, quint8 index,
                                          quint16 languageId, quint8 *buffer, quint32 bufferSize,
                                          quint32 *transferred);
using FnUsbQueryInterfaceSettings = BOOL(WINAPI *)(void *handle, quint8 alternateIndex,
                                                   RawUsbInterfaceDescriptor *descriptor);
using FnUsbQueryPipe = BOOL(WINAPI *)(void *handle, quint8 alternateSetting, quint8 pipeIndex,
                                      RawPipeInfo *pipe);
using FnUsbSetPipePolicy = BOOL(WINAPI *)(void *handle, quint8 endpoint, quint32 policy,
                                          quint32 valueSize, void *value);
using FnUsbReadPipe = BOOL(WINAPI *)(void *handle, quint8 endpoint, quint8 *buffer,
                                     quint32 bufferSize, quint32 *transferred,
                                     OVERLAPPED *overlapped);
using FnUsbWritePipe = BOOL(WINAPI *)(void *handle, quint8 endpoint, quint8 *buffer,
                                      quint32 bufferSize, quint32 *transferred,
                                      OVERLAPPED *overlapped);

QString fromDriverString(const char *value)
{
    return QString::fromLocal8Bit(value).trimmed();
}

QString stableIdentity(const RawDeviceInfo &device)
{
    const QString serial = fromDriverString(device.serialNumber);
    if (!serial.isEmpty())
        return serial;

    // Отсутствующий серийный номер — дефект дескриптора, а не повод склеить все
    // одинаковые устройства в одно. Instance ID переживает работу драйвера в пределах
    // текущего подключения и остаётся единственным доступным различителем.
    return fromDriverString(device.common.instanceId);
}

} // namespace

LibusbKLibrary &LibusbKLibrary::instance()
{
    static LibusbKLibrary library;
    return library;
}

bool LibusbKLibrary::load()
{
    if (m_loadAttempted)
        return m_loaded;
    m_loadAttempted = true;

    // Драйвер кладёт DLL в системный каталог Windows, который штатно входит в порядок
    // поиска библиотек. Не фиксируем букву системного диска: Windows нередко установлен
    // не на C:, а принудительный путь сломал бы именно такую машину.
    const QStringList candidates = {QStringLiteral("libusbK")};
    for (const QString &candidate : candidates) {
        m_library.setFileName(candidate);
        if (m_library.load()) {
            qCInfo(lcNab) << "libusbK loaded from" << m_library.fileName();
            break;
        }
    }

    if (!m_library.isLoaded()) {
        qCInfo(lcNab) << "libusbK is not installed; the NAB plugin stays idle";
        return false;
    }

    bool complete = true;
    const auto resolve = [this, &complete](const char *name) {
        QFunctionPointer symbol = m_library.resolve(name);
        if (!symbol)
            complete = false;
        return symbol;
    };

    m_lstInit = resolve("LstK_Init");
    m_lstFree = resolve("LstK_Free");
    m_lstMoveNext = resolve("LstK_MoveNext");
    m_usbInit = resolve("UsbK_Init");
    m_usbFree = resolve("UsbK_Free");
    m_usbGetDescriptor = resolve("UsbK_GetDescriptor");
    m_usbQueryInterfaceSettings = resolve("UsbK_QueryInterfaceSettings");
    m_usbQueryPipe = resolve("UsbK_QueryPipe");
    m_usbSetPipePolicy = resolve("UsbK_SetPipePolicy");
    m_usbReadPipe = resolve("UsbK_ReadPipe");
    m_usbWritePipe = resolve("UsbK_WritePipe");

    m_loaded = complete;
    if (!m_loaded)
        qCWarning(lcNab) << "libusbK found but does not export the expected API";
    return m_loaded;
}

bool LibusbKLibrary::isAvailable()
{
    return load();
}

QList<LibusbKLibrary::InterfaceInfo> LibusbKLibrary::enumerateNabInterfaces()
{
    QList<InterfaceInfo> result;
    if (!load())
        return result;

    void *list = nullptr;
    if (!reinterpret_cast<FnLstInit>(m_lstInit)(&list, 0) || !list)
        return result;

    const auto freeList = qScopeGuard([this, list] { reinterpret_cast<FnLstFree>(m_lstFree)(list); });
    const auto moveNext = reinterpret_cast<FnLstMoveNext>(m_lstMoveNext);
    const auto init = reinterpret_cast<FnUsbInit>(m_usbInit);

    RawDeviceInfo *device = nullptr;
    while (moveNext(list, &device) && device) {
        if (device->common.vendorId != kVendorId || device->common.productId != kProductId)
            continue;

        void *handle = nullptr;
        if (!init(&handle, device) || !handle)
            continue;
        const auto closeHandle = qScopeGuard([this, handle] {
            reinterpret_cast<FnUsbFree>(m_usbFree)(handle);
        });

        RawUsbDeviceDescriptor deviceDescriptor = {};
        quint32 descriptorSize = 0;
        const auto getDescriptor = reinterpret_cast<FnUsbGetDescriptor>(m_usbGetDescriptor);
        if (!getDescriptor(handle, kUsbDescriptorDevice, 0, 0,
                           reinterpret_cast<quint8 *>(&deviceDescriptor),
                           sizeof(deviceDescriptor), &descriptorSize)
            || descriptorSize != sizeof(deviceDescriptor)) {
            continue;
        }

        quint8 languages[4] = {};
        quint32 languageSize = 0;
        quint16 languageId = 0;
        if (getDescriptor(handle, kUsbDescriptorString, 0, 0, languages, sizeof(languages),
                          &languageSize)
            && languageSize >= sizeof(languages)) {
            languageId = quint16(languages[2]) | (quint16(languages[3]) << 8);
        }

        const auto readString = [getDescriptor, handle, languageId](quint8 index) {
            if (index == 0 || languageId == 0)
                return QString();

            quint8 buffer[256] = {};
            quint32 size = 0;
            if (!getDescriptor(handle, kUsbDescriptorString, index, languageId, buffer,
                               sizeof(buffer), &size)
                || size < 2) {
                return QString();
            }

            // libusbK 3.0 возвращает размер выделенного буфера на части старых драйверов,
            // поэтому доверяем длине из самого USB descriptor-а, а не LengthTransferred.
            const int descriptorLength = qMin<int>(buffer[0], sizeof(buffer));
            if (descriptorLength < 2 || (descriptorLength % 2) != 0)
                return QString();
            const int characterCount = (descriptorLength - 2) / 2;
            std::array<char16_t, 127> characters = {};
            std::memcpy(characters.data(), buffer + 2, characterCount * sizeof(char16_t));
            QString value = QString::fromUtf16(characters.data(), characterCount);
            // Прошивка NAB резервирует фиксированный размер строки и дополняет её NUL-ами.
            // Это допустимо для дескриптора, но нули нельзя отдавать в подпись виджета.
            value.remove(QChar::Null);
            return value.trimmed();
        };

        RawUsbInterfaceDescriptor interfaceDescriptor = {};
        if (!reinterpret_cast<FnUsbQueryInterfaceSettings>(m_usbQueryInterfaceSettings)(
                handle, 0, &interfaceDescriptor)) {
            continue;
        }

        InterfaceInfo info;
        info.identity = stableIdentity(*device);
        info.manufacturer = readString(deviceDescriptor.manufacturerIndex);
        info.product = readString(deviceDescriptor.productIndex);
        info.interfaceName = readString(interfaceDescriptor.interfaceIndex);
        info.serialNumber = readString(deviceDescriptor.serialNumberIndex);
        if (info.serialNumber.isEmpty())
            info.serialNumber = fromDriverString(device->serialNumber);
        info.interfaceNumber = interfaceDescriptor.interfaceNumber;
        info.usbVersion = deviceDescriptor.usbVersion;
        info.deviceVersion = deviceDescriptor.deviceVersion;
        info.interfaceClass = interfaceDescriptor.interfaceClass;
        info.interfaceSubClass = interfaceDescriptor.interfaceSubClass;
        info.interfaceProtocol = interfaceDescriptor.interfaceProtocol;

        const auto queryPipe = reinterpret_cast<FnUsbQueryPipe>(m_usbQueryPipe);
        for (quint8 index = 0; index < interfaceDescriptor.endpointCount; ++index) {
            RawPipeInfo pipe = {};
            if (!queryPipe(handle, interfaceDescriptor.alternateSetting, index, &pipe)
                || pipe.type != kBulkPipeType) {
                continue;
            }

            PipeInfo current{pipe.address, pipe.maxPacketSize};
            if ((current.address & kEndpointDirectionIn) != 0)
                info.input = current;
            else
                info.output = current;
        }

        // Терминалу требуется двунаправленный bulk-поток. Оставлять в списке интерфейс,
        // который нельзя ни читать, ни писать, означало бы обречь пользователя на ошибку
        // уже после выбора в UI.
        if (info.identity.isEmpty() || info.interfaceNumber < 0 || info.input.address == 0
            || info.output.address == 0) {
            continue;
        }
        qCDebug(lcNab).nospace() << "NAB interface " << info.interfaceNumber << ": "
                                 << QStringLiteral("OUT 0x%1 / IN 0x%2")
                                        .arg(info.output.address, 2, 16, QLatin1Char('0'))
                                        .arg(info.input.address, 2, 16, QLatin1Char('0'));
        result.append(info);
    }

    std::sort(result.begin(), result.end(), [](const InterfaceInfo &left,
                                               const InterfaceInfo &right) {
        if (left.identity != right.identity)
            return left.identity < right.identity;
        return left.interfaceNumber < right.interfaceNumber;
    });
    return result;
}

bool LibusbKLibrary::open(const QString &identity, int interfaceNumber, void **handle,
                          PipeInfo *input, PipeInfo *output, QString *error)
{
    if (handle)
        *handle = nullptr;
    if (input)
        *input = {};
    if (output)
        *output = {};

    if (!load()) {
        if (error)
            *error = QStringLiteral("libusbK is not installed");
        return false;
    }

    void *list = nullptr;
    if (!reinterpret_cast<FnLstInit>(m_lstInit)(&list, 0) || !list) {
        if (error)
            *error = errorText(GetLastError());
        return false;
    }
    const auto freeList = qScopeGuard([this, list] { reinterpret_cast<FnLstFree>(m_lstFree)(list); });

    const auto moveNext = reinterpret_cast<FnLstMoveNext>(m_lstMoveNext);
    RawDeviceInfo *device = nullptr;
    while (moveNext(list, &device) && device) {
        if (device->common.vendorId != kVendorId || device->common.productId != kProductId
            || device->common.interfaceNumber != interfaceNumber
            || stableIdentity(*device) != identity) {
            continue;
        }

        void *opened = nullptr;
        if (!reinterpret_cast<FnUsbInit>(m_usbInit)(&opened, device) || !opened) {
            if (error)
                *error = errorText(GetLastError());
            return false;
        }

        RawUsbInterfaceDescriptor interfaceDescriptor = {};
        if (!reinterpret_cast<FnUsbQueryInterfaceSettings>(m_usbQueryInterfaceSettings)(
                opened, 0, &interfaceDescriptor)) {
            if (error)
                *error = errorText(GetLastError());
            close(opened);
            return false;
        }

        PipeInfo foundInput;
        PipeInfo foundOutput;
        const auto queryPipe = reinterpret_cast<FnUsbQueryPipe>(m_usbQueryPipe);
        for (quint8 index = 0; index < interfaceDescriptor.endpointCount; ++index) {
            RawPipeInfo pipe = {};
            if (!queryPipe(opened, interfaceDescriptor.alternateSetting, index, &pipe)
                || pipe.type != kBulkPipeType) {
                continue;
            }
            const PipeInfo current{pipe.address, pipe.maxPacketSize};
            if ((current.address & kEndpointDirectionIn) != 0)
                foundInput = current;
            else
                foundOutput = current;
        }

        if (foundInput.address == 0 || foundOutput.address == 0) {
            if (error)
                *error = QStringLiteral("the interface has no bidirectional bulk endpoints");
            close(opened);
            return false;
        }

        if (handle)
            *handle = opened;
        if (input)
            *input = foundInput;
        if (output)
            *output = foundOutput;
        return true;
    }

    if (error)
        *error = QStringLiteral("the device is no longer connected");
    return false;
}

void LibusbKLibrary::close(void *handle)
{
    if (handle && m_loaded)
        reinterpret_cast<FnUsbFree>(m_usbFree)(handle);
}

bool LibusbKLibrary::setTransferTimeout(void *handle, quint8 endpoint, int milliseconds,
                                        QString *error)
{
    if (!handle || !m_loaded)
        return false;

    quint32 value = qMax(1, milliseconds);
    if (reinterpret_cast<FnUsbSetPipePolicy>(m_usbSetPipePolicy)(
            handle, endpoint, kPipeTransferTimeout, sizeof(value), &value)) {
        return true;
    }

    if (error)
        *error = errorText(GetLastError());
    return false;
}

LibusbKLibrary::TransferResult LibusbKLibrary::read(void *handle, quint8 endpoint, int maxBytes,
                                                     QByteArray *data)
{
    TransferResult result;
    if (data)
        data->clear();
    if (!handle || !m_loaded || maxBytes <= 0)
        return result;

    QByteArray buffer(maxBytes, '\0');
    quint32 transferred = 0;
    if (!reinterpret_cast<FnUsbReadPipe>(m_usbReadPipe)(
            handle, endpoint, reinterpret_cast<quint8 *>(buffer.data()), buffer.size(),
            &transferred, nullptr)) {
        result.errorCode = GetLastError();
        return result;
    }

    result.ok = true;
    result.transferred = transferred;
    if (data && transferred > 0)
        *data = buffer.left(transferred);
    return result;
}

LibusbKLibrary::TransferResult LibusbKLibrary::write(void *handle, quint8 endpoint,
                                                      const QByteArray &data)
{
    TransferResult result;
    if (!handle || !m_loaded)
        return result;

    quint32 transferred = 0;
    if (!reinterpret_cast<FnUsbWritePipe>(m_usbWritePipe)(
            handle, endpoint,
            reinterpret_cast<quint8 *>(const_cast<char *>(data.constData())), data.size(),
            &transferred, nullptr)) {
        result.errorCode = GetLastError();
        return result;
    }

    result.ok = true;
    result.transferred = transferred;
    return result;
}

bool LibusbKLibrary::isDisconnectError(quint32 errorCode)
{
    return errorCode == ERROR_DEVICE_NOT_CONNECTED || errorCode == ERROR_FILE_NOT_FOUND
        || errorCode == ERROR_PATH_NOT_FOUND || errorCode == ERROR_INVALID_HANDLE;
}

QString LibusbKLibrary::errorText(quint32 errorCode)
{
    wchar_t *message = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                            | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, errorCode, 0,
                                        reinterpret_cast<wchar_t *>(&message), 0, nullptr);
    if (length == 0 || !message)
        return QStringLiteral("Windows error %1").arg(errorCode);

    const QString text = QString::fromWCharArray(message, length).trimmed();
    LocalFree(message);
    return text;
}

} // namespace spotty
