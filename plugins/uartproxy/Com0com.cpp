/**
 * \file Com0com.cpp
 * \brief Реализация работы с com0com.
 */
#include "Com0com.h"

#include "UartProxyPlugin.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QThread>

#include <qt_windows.h>
#include <shellapi.h>

namespace spotty::uartproxy {

namespace {

constexpr auto kSetupGui = "setupg.exe";
constexpr auto kSetupCli = "setupc.exe";
constexpr auto kParametersKey = "SYSTEM\\CurrentControlSet\\Services\\com0com\\Parameters";

/// \brief Сколько ждать `setupc install`: установка устройств драйвером идёт десятки секунд.
constexpr int kInstallTimeoutMs = 120'000;

/// \brief Сколько ждать появления устройств после того, как `setupc` уже завершился.
constexpr int kAppearTimeoutMs = 10'000;

const wchar_t *wide(const QString &text)
{
    return reinterpret_cast<const wchar_t *>(text.utf16());
}

/**
 * \brief Строковое значение из HKLM.
 * \param view `KEY_WOW64_64KEY` или `KEY_WOW64_32KEY`: установщик com0com 32-битный и
 *        пишет в WOW6432Node, а Spotty бывает собран и так, и так.
 */
QString readString(HKEY root, const QString &subKey, const QString &name, REGSAM view)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, wide(subKey), 0, KEY_READ | view, &key) != ERROR_SUCCESS)
        return {};

    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, wide(name), nullptr, &type,
                                         reinterpret_cast<BYTE *>(buffer), &size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};
    return QString::fromWCharArray(buffer).trimmed();
}

/// \brief Флаг параметра порта: драйвер принимает и число, и строку `yes`.
bool readFlag(HKEY key, const wchar_t *name)
{
    BYTE buffer[64] = {};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, buffer, &size) != ERROR_SUCCESS)
        return false;
    if (type == REG_DWORD)
        return *reinterpret_cast<const DWORD *>(buffer) != 0;
    if (type == REG_SZ) {
        const QString text = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(buffer));
        return text.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0
            || text == QLatin1String("1");
    }
    return false;
}

bool keyExists(const QString &subKey)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, wide(subKey), 0, KEY_READ | KEY_WOW64_64KEY, &key)
        != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

/// \brief Каталог установки: тот, где нашлась хоть одна из программ настройки.
QString findInstallDir()
{
    QStringList directories;

    for (const REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
        directories << readString(HKEY_LOCAL_MACHINE, QStringLiteral("SOFTWARE\\com0com"),
                                  QStringLiteral("Install_Dir"), view);

        const QString uninstallKey =
            QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\com0com");
        directories << readString(HKEY_LOCAL_MACHINE, uninstallKey,
                                  QStringLiteral("InstallLocation"), view);

        QString uninstaller = readString(HKEY_LOCAL_MACHINE, uninstallKey,
                                         QStringLiteral("UninstallString"), view);
        uninstaller.remove(QLatin1Char('"'));
        if (!uninstaller.isEmpty())
            directories << QFileInfo(uninstaller).absolutePath();
    }

    // Последний довод — каталог по умолчанию: подписанные пересборки драйвера ставятся
    // туда же, но в реестр о себе пишут не всегда.
    for (const char *variable : {"ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"}) {
        const QString root = qEnvironmentVariable(variable);
        if (!root.isEmpty())
            directories << root + QStringLiteral("/com0com");
    }

    for (const QString &directory : std::as_const(directories)) {
        if (directory.isEmpty())
            continue;
        const QDir dir(directory);
        if (QFileInfo(dir.filePath(QLatin1String(kSetupCli))).isFile()
            || QFileInfo(dir.filePath(QLatin1String(kSetupGui))).isFile())
            return QDir::toNativeSeparators(dir.absolutePath());
    }
    return {};
}

/**
 * \brief Пары по параметрам драйвера (`Services\com0com\Parameters\CNCxN`).
 *
 * Не `HARDWARE\DEVICEMAP\SERIALCOMM`, как у QSerialPortInfo: скрытый конец пары оттуда
 * пропадает — ровно для этого он и скрыт, — а знать его Spotty как раз нужно. Параметры
 * переживают удаление пары, поэтому присутствие каждого конца проверяется отдельно.
 */
QList<Com0comPair> readPairs()
{
    HKEY parameters = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, wide(QLatin1String(kParametersKey)), 0,
                      KEY_READ | KEY_WOW64_64KEY, &parameters)
        != ERROR_SUCCESS)
        return {};

    static const QRegularExpression devicePattern(QStringLiteral("^CNC([AB])(\\d+)$"),
                                                  QRegularExpression::CaseInsensitiveOption);

    QMap<int, Com0comPair> pairs;
    for (DWORD index = 0;; ++index) {
        wchar_t name[256] = {};
        DWORD nameLength = DWORD(std::size(name));
        const LONG status =
            RegEnumKeyExW(parameters, index, name, &nameLength, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status != ERROR_SUCCESS)
            continue;

        const QString device = QString::fromWCharArray(name, int(nameLength)).toUpper();
        const QRegularExpressionMatch match = devicePattern.match(device);
        if (!match.hasMatch())
            continue;

        Com0comPort port;
        port.device = device;

        HKEY portKey = nullptr;
        if (RegOpenKeyExW(parameters, name, 0, KEY_READ, &portKey) == ERROR_SUCCESS) {
            wchar_t value[256] = {};
            DWORD valueSize = sizeof(value) - sizeof(wchar_t);
            DWORD type = 0;
            if (RegQueryValueExW(portKey, L"PortName", nullptr, &type,
                                 reinterpret_cast<BYTE *>(value), &valueSize)
                    == ERROR_SUCCESS
                && type == REG_SZ) {
                port.name = QString::fromWCharArray(value).trimmed();
            }
            port.hidden = readFlag(portKey, L"HiddenMode");
            RegCloseKey(portKey);
        }
        // «-» и пустое значение — имя по умолчанию, то есть имя самого устройства.
        if (port.name.isEmpty() || port.name == QLatin1String("-"))
            port.name = device;

        const int number = match.captured(2).toInt();
        Com0comPair &pair = pairs[number];
        pair.number = number;
        (match.captured(1).compare(QLatin1String("A"), Qt::CaseInsensitive) == 0 ? pair.a
                                                                                 : pair.b) = port;
    }
    RegCloseKey(parameters);

    QList<Com0comPair> present;
    for (const Com0comPair &pair : std::as_const(pairs)) {
        if (!pair.a.device.isEmpty() && !pair.b.device.isEmpty()
            && (portNameTaken(pair.a.name) || portNameTaken(pair.b.name)))
            present.append(pair);
    }
    return present;
}

/**
 * \brief Путь устройства для QSerialPort: `\\.\CNCB0`.
 *
 * QSerialPort дописывает префикс `\\.\` только к именам, начинающимся с `COM`, а остальные
 * открывает как есть, то есть как файл в текущем каталоге. Скрытый конец пары зовётся
 * `CNCBn`, и без явного префикса открытие падало с «The system cannot find the file
 * specified», хотя устройство на месте.
 */
QString devicePath(const QString &name)
{
    const QString prefix = QStringLiteral("\\\\.\\");
    return name.startsWith(prefix) ? name : prefix + name;
}

/**
 * \brief Запустить `setupc.exe` с правами администратора.
 * \param wait Дождаться завершения (только из потока ввода-вывода).
 * \param cancelled Пользователь отказал в повышении прав.
 */
bool runElevated(const QString &program, const QString &arguments, bool wait, bool *cancelled,
                 QString *error)
{
    *cancelled = false;
    const QString directory = QDir::toNativeSeparators(QFileInfo(program).absolutePath());

    SHELLEXECUTEINFOW execute = {};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_FLAG_NO_UI | (wait ? SEE_MASK_NOCLOSEPROCESS : 0);
    // «runas», а не «open»: у setupc.exe манифест не обязан требовать прав сам, а без них
    // он молча не сможет поставить устройство.
    execute.lpVerb = L"runas";
    execute.lpFile = wide(program);
    execute.lpParameters = wide(arguments);
    // setupc ищет com0com.inf в текущем каталоге — без этого установка пары не находит драйвер.
    execute.lpDirectory = wide(directory);
    execute.nShow = SW_HIDE;

    if (!ShellExecuteExW(&execute)) {
        const DWORD code = GetLastError();
        if (code == ERROR_CANCELLED) {
            *cancelled = true;
            return false;
        }
        if (error)
            *error = UartProxyPlugin::tr("Could not start %1: %2")
                         .arg(program, qt_error_string(int(code)));
        return false;
    }

    if (execute.hProcess) {
        WaitForSingleObject(execute.hProcess, kInstallTimeoutMs);
        CloseHandle(execute.hProcess);
    }
    return true;
}

} // namespace

QString Com0comInfo::setupGui() const
{
    if (installDir.isEmpty())
        return {};
    const QString path = QDir(installDir).filePath(QLatin1String(kSetupGui));
    return QFileInfo(path).isFile() ? QDir::toNativeSeparators(path) : QString();
}

QString Com0comInfo::setupCli() const
{
    if (installDir.isEmpty())
        return {};
    const QString path = QDir(installDir).filePath(QLatin1String(kSetupCli));
    return QFileInfo(path).isFile() ? QDir::toNativeSeparators(path) : QString();
}

Com0comInfo detectCom0com()
{
    Com0comInfo info;
    info.driverInstalled =
        keyExists(QStringLiteral("SYSTEM\\CurrentControlSet\\Services\\com0com"));
    info.installDir = findInstallDir();
    info.pairs = readPairs();
    return info;
}

const Com0comPair *findCom0comPair(const Com0comInfo &info, const QString &name,
                                   Com0comPort *ownEnd, Com0comPort *peerEnd)
{
    for (const Com0comPair &pair : info.pairs) {
        const bool isA = pair.a.name.compare(name, Qt::CaseInsensitive) == 0
                      || pair.a.device.compare(name, Qt::CaseInsensitive) == 0;
        const bool isB = pair.b.name.compare(name, Qt::CaseInsensitive) == 0
                      || pair.b.device.compare(name, Qt::CaseInsensitive) == 0;
        if (!isA && !isB)
            continue;
        if (ownEnd)
            *ownEnd = isA ? pair.a : pair.b;
        if (peerEnd)
            *peerEnd = isA ? pair.b : pair.a;
        return &pair;
    }
    return nullptr;
}

bool portNameTaken(const QString &name)
{
    if (name.isEmpty())
        return false;
    // Символическая ссылка в пространстве DOS-имён есть у любого порта, видимого или
    // скрытого, — это то самое имя, по которому его открывают.
    wchar_t target[MAX_PATH] = {};
    return QueryDosDeviceW(wide(name), target, MAX_PATH) != 0;
}

bool isValidPortName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_#]+$"));
    return pattern.match(name).hasMatch();
}

QString ensureCom0comPair(const QString &visibleName, QString *error, int *createdPair)
{
    const auto failWith = [error](const QString &message) {
        if (error)
            *error = message;
        return QString();
    };

    Com0comInfo info = detectCom0com();
    if (!info.driverInstalled)
        return failWith(UartProxyPlugin::tr("com0com is not installed: Spotty needs it to "
                                            "create a virtual port."));

    Com0comPort peer;
    if (findCom0comPair(info, visibleName, nullptr, &peer))
        return devicePath(peer.name);

    if (!isValidPortName(visibleName))
        return failWith(UartProxyPlugin::tr("\"%1\" is not a valid port name.").arg(visibleName));
    if (portNameTaken(visibleName))
        return failWith(
            UartProxyPlugin::tr("%1 is already used by another device.").arg(visibleName));

    const QString setup = info.setupCli();
    if (setup.isEmpty())
        return failWith(UartProxyPlugin::tr("The com0com program setupc.exe was not found."));

    // Видимый конец получает выбранное имя, второй — имя по умолчанию (CNCBn) и скрывается
    // от перечисления: чужой программе в списке портов он ни к чему.
    const QString arguments =
        QStringLiteral("--silent install PortName=%1 PortName=-,HiddenMode=yes").arg(visibleName);

    bool cancelled = false;
    QString problem;
    if (!runElevated(setup, arguments, /*wait=*/true, &cancelled, &problem)) {
        return failWith(
            cancelled ? UartProxyPlugin::tr("Administrator rights are needed to create %1.")
                            .arg(visibleName)
                      : problem);
    }

    // setupc возвращается, когда драйвер уже принял пару, но символические ссылки на
    // устройства появляются чуть позже.
    const QDeadlineTimer deadline(kAppearTimeoutMs);
    while (!deadline.hasExpired()) {
        info = detectCom0com();
        const Com0comPair *pair = findCom0comPair(info, visibleName, nullptr, &peer);
        if (pair && portNameTaken(peer.name)) {
            if (createdPair)
                *createdPair = pair->number;
            return devicePath(peer.name);
        }
        QThread::msleep(200);
    }
    return failWith(
        UartProxyPlugin::tr("com0com did not create %1. Try the com0com setup.").arg(visibleName));
}

bool removeCom0comPair(const Com0comInfo &info, int number, QString *error)
{
    const QString setup = info.setupCli();
    if (setup.isEmpty()) {
        if (error)
            *error = UartProxyPlugin::tr("The com0com program setupc.exe was not found.");
        return false;
    }

    bool cancelled = false;
    if (runElevated(setup, QStringLiteral("--silent remove %1").arg(number), /*wait=*/false,
                    &cancelled, error))
        return true;
    return cancelled;
}

bool launchCom0comSetup(const Com0comInfo &info, QString *error)
{
    const QString setup = info.setupGui();
    if (setup.isEmpty()) {
        if (error)
            *error = UartProxyPlugin::tr("The com0com setup program was not found.");
        return false;
    }

    const QString directory = QDir::toNativeSeparators(QFileInfo(setup).absolutePath());

    SHELLEXECUTEINFOW execute = {};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_FLAG_NO_UI;
    execute.lpVerb = L"open";
    execute.lpFile = wide(setup);
    execute.lpDirectory = wide(directory);
    execute.nShow = SW_SHOWNORMAL;

    // Вызов ждёт, пока пользователь ответит на запрос UAC; поверх диалога в это время всё
    // равно стоит защищённый рабочий стол, так что зависания со стороны не видно.
    if (ShellExecuteExW(&execute))
        return true;

    const DWORD code = GetLastError();
    if (code == ERROR_CANCELLED)
        return true; // пользователь сам отказал в повышении прав — это не ошибка

    if (error)
        *error = UartProxyPlugin::tr("Could not start %1: %2")
                     .arg(setup, qt_error_string(int(code)));
    return false;
}

} // namespace spotty::uartproxy
