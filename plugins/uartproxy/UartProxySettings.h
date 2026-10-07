/**
 * \file UartProxySettings.h
 * \brief Ключи настроек плагина перехвата порта.
 *
 * Вынесены в заголовок: схему строит плагин, а читает канал, и расхождение в строке ключа
 * не поймает ни компилятор, ни тесты — настройка просто перестанет действовать.
 */
#pragma once

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

/// \brief Где на POSIX положить ссылку на созданный виртуальный порт.
inline constexpr auto kLinkPath = "linkPath";

/**
 * \brief Виртуальный конец.
 *
 * На POSIX — готовый порт вместо созданного pty. На Windows — имя видимого конца пары
 * com0com, которое открывает чужая программа; сам Spotty открывает второй, скрытый конец.
 */
inline constexpr auto kVirtualPort = "virtualPort";

/// \brief Строка состояния com0com (только Windows; значения не хранит).
inline constexpr auto kCom0comStatus = "com0comStatus";

/// \brief Кнопка «открыть настройку com0com» (только Windows; значения не хранит).
inline constexpr auto kCom0comSetup = "com0comSetup";

/// \brief Кнопка «удалить пару com0com» (только Windows; значения не хранит).
inline constexpr auto kCom0comRemove = "com0comRemove";

/// \brief Значение #kLinkPath по умолчанию.
inline constexpr auto kDefaultLinkPath = "/tmp/spotty-uart";

} // namespace spotty::uartproxy
