/**
 * \file JlinkArmLibrary.cpp
 * \brief Реализация spotty::JlinkArmLibrary.
 *
 * Объявления структур и сигнатур ниже — не заголовок SEGGER SDK (он не входит в
 * поставку J-Link software, только в отдельный SDK-пакет), а минимальный набор,
 * восстановленный по официальной документации UM08001 и раскладке, стабильной уже много
 * лет и используемой сторонними обёртками библиотеки. Раскладка структур обязана
 * побайтово совпадать с тем, что ожидает сама libjlinkarm — это FFI, не C++ API.
 */
#include "JlinkArmLibrary.h"

#include <QLoggingCategory>
#include <QVector>

namespace spotty {

namespace {

Q_LOGGING_CATEGORY(lcJlinkArm, "spotty.plugins.jlinkrtt")

constexpr int kHostIfUsb = 1; ///< Бит-флаг для JLINK_EMU_GetList(): искать по USB.

constexpr int kTifJtag = 0;
constexpr int kTifSwd = 1;

constexpr int kRttCmdStart = 0;
constexpr int kRttCmdStop = 1;
constexpr int kRttCmdGetStat = 4;

#pragma pack(push, 1)
/// \brief Раскладка `JLINK_EMU_CONNECT_INFO` — один найденный зонд.
struct RawEmuConnectInfo
{
    quint32 SerialNumber;
    quint32 Connection;
    quint32 USBAddr;
    quint8 aIPAddr[16];
    qint32 Time;
    quint64 Time_us;
    quint32 HWVersion;
    quint8 abMACAddr[6];
    char acProduct[32];
    char acNickName[32];
    char acFWString[112];
    char IsDHCPAssignedIP;
    char IsDHCPAssignedIPIsValid;
    char NumIPConnections;
    char NumIPConnectionsIsValid;
    quint8 aPadding[34];
};

/// \brief Раскладка `JLINK_RTTERMINAL_STATUS` — ответ команды GETSTAT.
struct RawRttStatus
{
    quint32 NumBytesTransferred;
    quint32 NumBytesRead;
    qint32 HostOverflowCount;
    qint32 IsRunning;
    qint32 NumUpBuffers;
    qint32 NumDownBuffers;
    qint32 Reserved[2];
};
#pragma pack(pop)

using JlinkLogFunc = void (*)(const char *line);

using FnEmuGetList = int (*)(int hostIfs, RawEmuConnectInfo *info, int maxInfos);
using FnEmuSelectByUsbSn = int (*)(quint32 serial);
using FnOpenEx = const char *(*)(JlinkLogFunc log, JlinkLogFunc err);
using FnClose = void (*)();
using FnTifSelect = int (*)(int interfaceType);
using FnSetSpeed = void (*)(quint32 speedKhz);
using FnConnect = int (*)();
using FnExecCommand = int (*)(const char *cmd, char *result, int maxResultLen);
using FnRttControl = int (*)(int cmd, void *data);
using FnRttRead = int (*)(int bufferIndex, char *buffer, int bufferSize);
using FnRttWrite = int (*)(int bufferIndex, const char *buffer, int numBytes);

} // namespace

JlinkArmLibrary &JlinkArmLibrary::instance()
{
    static JlinkArmLibrary library;
    return library;
}

bool JlinkArmLibrary::load()
{
    if (m_loadAttempted)
        return m_loaded;
    m_loadAttempted = true;

    // Пакет J-Link software не кладёт библиотеку в системные пути поиска библиотек — она
    // самодостаточна внутри собственного каталога установки. Пробуем известное место
    // установки для каждой ОС, а следом — голое имя на случай, если пользователь сам
    // прописал путь через переменные окружения загрузчика.
    const QStringList candidates = {
#if defined(Q_OS_MACOS)
        QStringLiteral("/Applications/SEGGER/JLink/libjlinkarm.dylib"),
        QStringLiteral("jlinkarm"),
#elif defined(Q_OS_WIN)
        QStringLiteral("JLink_x64"),
        QStringLiteral("JLinkARM"),
        QStringLiteral("C:/Program Files/SEGGER/JLink/JLink_x64.dll"),
        QStringLiteral("C:/Program Files (x86)/SEGGER/JLink/JLinkARM.dll"),
#else
        QStringLiteral("jlinkarm"),
        QStringLiteral("/opt/SEGGER/JLink/libjlinkarm.so"),
#endif
    };

    for (const QString &candidate : candidates) {
        m_library.setFileName(candidate);
        if (m_library.load())
            break;
    }

    if (!m_library.isLoaded()) {
        qCWarning(lcJlinkArm) << "libjlinkarm not found - install SEGGER J-Link software to "
                                 "use RTT interfaces";
        return false;
    }

    bool ok = true;
    const auto resolve = [this, &ok](const char *name) -> QFunctionPointer {
        QFunctionPointer symbol = m_library.resolve(name);
        if (!symbol)
            ok = false;
        return symbol;
    };

    m_fnEmuGetList = resolve("JLINK_EMU_GetList");
    m_fnEmuSelectByUsbSn = resolve("JLINK_EMU_SelectByUSBSN");
    m_fnOpenEx = resolve("JLINK_OpenEx");
    m_fnClose = resolve("JLINK_Close");
    m_fnTifSelect = resolve("JLINK_TIF_Select");
    m_fnSetSpeed = resolve("JLINK_SetSpeed");
    m_fnConnect = resolve("JLINK_Connect");
    m_fnExecCommand = resolve("JLINK_ExecCommand");
    m_fnRttControl = resolve("JLINK_RTTERMINAL_Control");
    m_fnRttRead = resolve("JLINK_RTTERMINAL_Read");
    m_fnRttWrite = resolve("JLINK_RTTERMINAL_Write");

    if (!ok) {
        qCWarning(lcJlinkArm) << "libjlinkarm loaded but is missing expected symbols - "
                                 "unsupported DLL version?";
        m_library.unload();
        return false;
    }

    m_loaded = true;
    return true;
}

QList<JlinkArmLibrary::ProbeInfo> JlinkArmLibrary::enumerateProbes()
{
    QMutexLocker locker(&m_mutex);
    if (!load())
        return {};

    constexpr int kMaxProbes = 32;
    QVector<RawEmuConnectInfo> raw(kMaxProbes);
    const auto emuGetList = reinterpret_cast<FnEmuGetList>(m_fnEmuGetList);
    const int count = emuGetList(kHostIfUsb, raw.data(), kMaxProbes);

    QList<ProbeInfo> result;
    if (count <= 0)
        return result;

    result.reserve(qMin(count, kMaxProbes));
    for (int i = 0; i < count && i < kMaxProbes; ++i) {
        ProbeInfo info;
        info.serialNumber = raw[i].SerialNumber;
        // acProduct не гарантированно завершена нулём на всю длину поля — fromLatin1 с
        // явной длиной через QByteArray безопаснее, чем полагаться на strlen внутри Qt.
        info.product = QString::fromLatin1(
            QByteArray(raw[i].acProduct, qstrnlen(raw[i].acProduct, sizeof(raw[i].acProduct))));
        result.append(info);
    }
    return result;
}

bool JlinkArmLibrary::openConnection(quint32 serialNumber, QString *error)
{
    QMutexLocker locker(&m_mutex);
    if (!load()) {
        if (error)
            *error = QStringLiteral("libjlinkarm not found");
        return false;
    }

    if (m_connectionOpen) {
        if (error)
            *error = QStringLiteral("another J-Link connection is already open in this process");
        return false;
    }

    const auto selectByUsbSn = reinterpret_cast<FnEmuSelectByUsbSn>(m_fnEmuSelectByUsbSn);
    if (selectByUsbSn(serialNumber) < 0) {
        if (error)
            *error = QStringLiteral("JLINK_EMU_SelectByUSBSN failed for S/N %1")
                         .arg(serialNumber);
        return false;
    }

    const auto openEx = reinterpret_cast<FnOpenEx>(m_fnOpenEx);
    const char *openResult = openEx(nullptr, nullptr);
    if (openResult && *openResult) {
        if (error)
            *error = QString::fromLatin1(openResult);
        return false;
    }

    m_connectionOpen = true;
    return true;
}

void JlinkArmLibrary::closeConnection()
{
    QMutexLocker locker(&m_mutex);
    if (!m_connectionOpen)
        return;
    reinterpret_cast<FnClose>(m_fnClose)();
    m_connectionOpen = false;
}

bool JlinkArmLibrary::isConnectionOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_connectionOpen;
}

bool JlinkArmLibrary::selectInterfaceAndSpeed(TargetInterface targetInterface, int speedKhz)
{
    QMutexLocker locker(&m_mutex);
    if (!m_loaded || !m_connectionOpen)
        return false;

    // JLINK_TIF_Select() своим кодом возврата ненадёжно сообщает об успехе — реальная
    // проверка происходит позже, через JLINK_Connect(). Здесь просто выставляем параметры.
    reinterpret_cast<FnTifSelect>(m_fnTifSelect)(targetInterface == Jtag ? kTifJtag : kTifSwd);
    reinterpret_cast<FnSetSpeed>(m_fnSetSpeed)(quint32(qMax(1, speedKhz)));
    return true;
}

bool JlinkArmLibrary::connectTarget(const QString &deviceName, QString *error)
{
    QMutexLocker locker(&m_mutex);
    if (!m_loaded || !m_connectionOpen)
        return false;

    if (!deviceName.trimmed().isEmpty()) {
        const QByteArray cmd = "device = " + deviceName.trimmed().toLatin1();
        char result[256] = {};
        reinterpret_cast<FnExecCommand>(m_fnExecCommand)(cmd.constData(), result,
                                                          int(sizeof(result)));
        // Текст result у этой команды не даёт надёжного признака успеха/неудачи в
        // публичном API — настоящая проверка ниже, через JLINK_Connect().
    }

    const int rc = reinterpret_cast<FnConnect>(m_fnConnect)();
    if (rc < 0) {
        if (error)
            *error = QStringLiteral("JLINK_Connect failed (code %1)").arg(rc);
        return false;
    }
    return true;
}

bool JlinkArmLibrary::rttStart(QString *error)
{
    QMutexLocker locker(&m_mutex);
    if (!m_loaded || !m_connectionOpen)
        return false;

    // nullptr вместо адреса управляющего блока — библиотека сама ищет сигнатуру "SEGGER
    // RTT" в памяти таргета. Поиск асинхронный: успешный запуск команды не означает, что
    // блок уже найден, это выясняется позже через GETSTAT.
    const int rc = reinterpret_cast<FnRttControl>(m_fnRttControl)(kRttCmdStart, nullptr);
    if (rc < 0) {
        if (error)
            *error = QStringLiteral("JLINK_RTTERMINAL_Control(START) failed (code %1)").arg(rc);
        return false;
    }
    return true;
}

void JlinkArmLibrary::rttStop()
{
    QMutexLocker locker(&m_mutex);
    if (!m_loaded || !m_connectionOpen)
        return;
    reinterpret_cast<FnRttControl>(m_fnRttControl)(kRttCmdStop, nullptr);
}

QByteArray JlinkArmLibrary::rttRead(int channel, int maxBytes, bool *running)
{
    QMutexLocker locker(&m_mutex);
    if (running)
        *running = false;
    if (!m_loaded || !m_connectionOpen)
        return {};

    const auto rttControl = reinterpret_cast<FnRttControl>(m_fnRttControl);
    RawRttStatus status{};
    if (rttControl(kRttCmdGetStat, &status) >= 0 && running)
        *running = status.IsRunning != 0;

    QByteArray buffer(maxBytes, Qt::Uninitialized);
    const int got =
        reinterpret_cast<FnRttRead>(m_fnRttRead)(channel, buffer.data(), int(buffer.size()));
    if (got <= 0)
        return {};
    buffer.truncate(got);
    return buffer;
}

qint64 JlinkArmLibrary::rttWrite(int channel, const QByteArray &data)
{
    QMutexLocker locker(&m_mutex);
    if (!m_loaded || !m_connectionOpen)
        return -1;
    return reinterpret_cast<FnRttWrite>(m_fnRttWrite)(channel, data.constData(),
                                                       int(data.size()));
}

} // namespace spotty
