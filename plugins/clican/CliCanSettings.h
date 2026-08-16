/**
 * \file CliCanSettings.h
 * \brief Ключи настроек транспорта CLI-CAN.
 *
 * Один заголовок на схему и на канал: ключ — это строка, которую не проверяет ни
 * компилятор, ни тест, и опечатка в ней выглядит как «настройка не сохраняется». В
 * jlinkrtt те же ключи объявлены дважды с припиской «должны совпадать со схемой» — здесь
 * совпадать нечему.
 */
#pragma once

#include "CliCanProtocol.h"

namespace spotty::clican {

/// \brief Номер узла на шине; поле с живым списком (SettingsField::live).
inline constexpr auto kNodeKey = "node";

/// \brief Скорость шины в битах в секунду.
inline constexpr auto kBitrateKey = "bitrate";

/// \brief Как часто слать пустой пакет, пока пользователь ничего не набирает.
inline constexpr auto kKeepAliveKey = "keepAliveMs";

/// \brief Через сколько молчания узла предупредить пользователя.
inline constexpr auto kTimeoutKey = "responseTimeoutMs";

/// \brief Скорость по умолчанию — самая распространённая на платах.
inline constexpr int kDefaultBitrate = 500'000;

/**
 * \brief Удержание туннеля по умолчанию.
 *
 * Пятая часть от clican::kTunnelHoldMs: запас на потерянный кадр и на занятую шину, но не
 * настолько частый трафик, чтобы мешать остальным участникам.
 */
inline constexpr int kDefaultKeepAliveMs = 1000;

/// \brief Молчание, после которого стоит предупредить пользователя.
inline constexpr int kDefaultTimeoutMs = 3000;

} // namespace spotty::clican
