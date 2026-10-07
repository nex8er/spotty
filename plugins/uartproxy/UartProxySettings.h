/**
 * \file UartProxySettings.h
 * \brief Ключи настроек плагина перехвата порта.
 *
 * Вынесены в заголовок: схему строит плагин, а читает канал, и расхождение в строке ключа
 * не поймает ни компилятор, ни тесты — настройка просто перестанет действовать.
 */
#pragma once

#include <QString>
#include <QVariantMap>
#include <QtGlobal>

namespace spotty::uartproxy {

/// \brief Реальный порт с устройством: устойчивый идентификатор либо системное имя.
inline constexpr auto kRealPort = "realPort";

inline constexpr auto kBaudRate = "baudRate";
inline constexpr auto kDataBits = "dataBits";
inline constexpr auto kParity = "parity";
inline constexpr auto kStopBits = "stopBits";
inline constexpr auto kFlowControl = "flowControl";
inline constexpr auto kDtrOnOpen = "dtrOnOpen";
inline constexpr auto kRtsOnOpen = "rtsOnOpen";

/**
 * \brief Имя виртуального порта на POSIX — то, что открывает чужая программа.
 *
 * Путь в `/dev` попадает в списки портов других программ (ссылка там создаётся с правами
 * администратора), любой другой — только если ввести его руками.
 */
inline constexpr auto kLinkPath = "linkPath";

/**
 * \brief Виртуальный конец.
 *
 * Только Windows: имя видимого конца пары com0com, которое открывает чужая программа; сам
 * Spotty открывает второй, скрытый конец. На POSIX ключа нет — там #kLinkPath, см.
 * #visiblePortName.
 */
inline constexpr auto kVirtualPort = "virtualPort";

/// \brief Строка состояния com0com (только Windows; значения не хранит).
inline constexpr auto kCom0comStatus = "com0comStatus";

/// \brief Кнопка «открыть настройку com0com» (только Windows; значения не хранит).
inline constexpr auto kCom0comSetup = "com0comSetup";

/// \brief Кнопка «удалить пару com0com» (только Windows; значения не хранит).
inline constexpr auto kCom0comRemove = "com0comRemove";

/// \brief Строка состояния имени порта (только POSIX; значения не хранит).
inline constexpr auto kLinkStatus = "linkStatus";

/// \brief Кнопка «удалить имя из /dev» (только POSIX; значения не хранит).
inline constexpr auto kLinkRemove = "linkRemove";

/// \brief Имя без прав администратора: ссылка вне `/dev`, в списках других программ её нет.
inline constexpr auto kUnprivilegedLinkPath = "/tmp/spotty-uart";

/**
 * \brief Значение #kLinkPath по умолчанию.
 *
 * На Linux — имя в `/dev`, которое видно в списках портов (создаётся с правами
 * администратора). На macOS создать имя в `/dev` нельзя совсем, и умолчание — путь вне него.
 */
#ifdef Q_OS_MACOS
inline constexpr auto kDefaultLinkPath = kUnprivilegedLinkPath;
#else
inline constexpr auto kDefaultLinkPath = "/dev/ttyV0";
#endif

/**
 * \brief Имя, которое откроет чужая программа, из настроек канала.
 *
 * На Windows это #kVirtualPort, на POSIX — #kLinkPath. Платформа выбирает ключ сама, а не
 * по тому, какой из них заполнен: в файле настроек мог остаться ключ от схемы другой
 * платформы или прежней версии, и читать его значило бы показывать и открывать то, чего в
 * диалоге уже нет.
 */
inline QString visiblePortName(const QVariantMap &settings)
{
#ifdef Q_OS_WIN
    return settings.value(QLatin1String(kVirtualPort)).toString().trimmed();
#else
    return settings.value(QLatin1String(kLinkPath)).toString().trimmed();
#endif
}

} // namespace spotty::uartproxy
