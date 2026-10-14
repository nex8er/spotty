/**
 * \file TcpTunnelSettings.h
 * \brief Ключи настроек транспорта tcptunnel и их умолчания.
 */
#pragma once

namespace spotty::tcptunnel {

/// \brief Адрес или имя машины, где работает агент. Ключ схемы — общий для канала и плагина.
inline constexpr auto kHost = "host";

/// \brief TCP-порт агента. Умолчание — kDefaultPort.
inline constexpr auto kPort = "port";

/// \brief Переподключаться ли самому, когда соединение оборвалось.
inline constexpr auto kAutoReconnect = "autoReconnect";

/// \brief Сколько ждать соединения при открытии канала, мс.
inline constexpr auto kConnectTimeoutMs = "connectTimeoutMs";

/// \brief Диапазон поиска агентов; пусто — сети этого компьютера.
inline constexpr auto kScanRange = "scanRange";

/// \brief Первый порт, с которого сканер начинает у каждого хоста.
inline constexpr auto kScanPort = "scanPort";

/// \brief Ключ действия «искать в сети» (не значение настроек).
inline constexpr auto kScanAction = "scan";

/// \brief Ключ строки хода и результата поиска (не значение настроек).
inline constexpr auto kScanStatus = "scanStatus";

/// \name Установка агента по ssh
///
/// Ключи kSsh* — не настройки подключения: их спрашивает окно перед действием
/// (IInterfacePlugin::promptSchema()), и нигде, кроме памяти этого окна, они не хранятся.
/// @{
inline constexpr auto kSshUser = "sshUser";
inline constexpr auto kSshPassword = "sshPassword";
inline constexpr auto kSshKeyFile = "sshKeyFile";
inline constexpr auto kSshKeyPassphrase = "sshKeyPassphrase";
inline constexpr auto kSshPort = "sshPort";
inline constexpr auto kRemoteUart = "remoteUart";   ///< Последовательный порт удалённой машины.
inline constexpr auto kBaud = "baud";
inline constexpr auto kFormat = "format";
inline constexpr auto kFlow = "flow";
inline constexpr auto kBind = "bind";
inline constexpr auto kAllow = "allow";
inline constexpr auto kAutostart = "autostart";

/// Ключи действий и строки состояния (не значения настроек).
inline constexpr auto kForgetKeyAction = "forgetKey";
inline constexpr auto kFindPortsAction = "findPorts";
inline constexpr auto kInstallAction = "install";
inline constexpr auto kUninstallAction = "uninstall";
inline constexpr auto kRemoteStatus = "remoteStatus";
/// @}

/// \brief Порт агента по умолчанию; следующие экземпляры на той же машине — 7778, 7779…
inline constexpr int kDefaultPort = 7777;

} // namespace spotty::tcptunnel
