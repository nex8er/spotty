/**
 * \file AppContext.h
 * \brief Набор долгоживущих служб, передаваемый в UI.
 */
#pragma once

namespace spotty {

class InterfaceRegistry;
class PluginManager;
class SettingsStore;
class ThemeManager;

/**
 * \struct AppContext
 * \brief Службы, которые нужны интерфейсу; собираются в main() и передаются в MainWindow.
 *
 * Явная передача вместо обращения к одиночкам делает зависимости каждого виджета
 * видимыми прямо в его конструкторе. Это начнёт окупаться, как только появятся панели:
 * по сигнатуре сразу понятно, что панель логирования трогает настройки, а панель поиска —
 * нет.
 *
 * \note Ни один указатель не принадлежит структуре. Все объекты созданы в main() на стеке
 *       и переживают окно.
 */
struct AppContext
{
    SettingsStore *settings = nullptr;   ///< Общие настройки (`settings.json`).
    PluginManager *plugins = nullptr;    ///< Загруженные плагины.
    InterfaceRegistry *registry = nullptr; ///< Доступные устройства.
    ThemeManager *theme = nullptr;       ///< Оформление и палитра.
};

} // namespace spotty
