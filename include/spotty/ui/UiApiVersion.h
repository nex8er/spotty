/**
 * \file UiApiVersion.h
 * \brief Версия ABI панельных плагинов и идентификатор их интерфейса.
 */
#pragma once

#include <spotty/api/ApiVersion.h>

/**
 * \def SPOTTY_UI_API_VERSION
 * \brief Версия двоичного интерфейса панельных плагинов.
 *
 * Отдельная от #SPOTTY_API_VERSION намеренно. Плагины интерфейсов и плагины панелей —
 * независимые семейства с разными сроками жизни: правка панельного контракта не должна
 * заставлять пересобирать UART, а spotty::PluginManager — отвергать его с причиной, к
 * которой транспорт непричастен.
 *
 * \note Всё, что сказано в #SPOTTY_API_VERSION про ломающие изменения, действует и здесь,
 *       включая добавление виртуального метода в конец класса.
 */
#define SPOTTY_UI_API_VERSION 6

/**
 * \def SPOTTY_PANEL_PLUGIN_IID
 * \brief Идентификатор интерфейса для Q_DECLARE_INTERFACE и Q_PLUGIN_METADATA.
 *
 * Второй рубеж защиты, проверяемый самим Qt, — как #SPOTTY_INTERFACE_PLUGIN_IID для
 * транспортов.
 */
#define SPOTTY_PANEL_PLUGIN_IID "org.spotty.IPanelPlugin/1.0"

// Панельный SDK построен поверх основного и пользуется его типами: SettingsSchema,
// ChannelState, DataDirection, IDataFilter. Ломающее изменение там ломает и панели, но
// заметить это без напоминания невозможно — версии-то разные. Ассерт срабатывает при
// компиляции у того, кто поднял основную версию, а не у пользователя при загрузке.
// Ровно этот случай уже наступил при переходе на SPOTTY_API_VERSION 2: поле
// SettingsField::live изменило раскладку SettingsSchema, а её панельные плагины возвращают
// из IPanelPlugin::settingsSchema() — то есть чужая правка дотянулась до панельного ABI, и
// SPOTTY_UI_API_VERSION пришлось поднять вместе с основной. Виртуальный метод, добавленный
// в IInterfacePlugin, сам по себе панелей бы не задел.
static_assert(SPOTTY_API_VERSION == 2,
              "SPOTTY_API_VERSION changed: review the panel SDK, bump "
              "SPOTTY_UI_API_VERSION if the change reaches it, and update this assert");
