/**
 * \file AppContext.h
 * \brief Набор долгоживущих служб, передаваемый в UI.
 */
#pragma once

namespace spotty {

class HistoryStore;
class InterfaceRegistry;
class PanelPluginRegistry;
class PluginManager;
class Session;
class SettingsStore;
class ThemeManager;

/**
 * \struct AppContext
 * \brief Службы, которые нужны интерфейсу; собираются в main() и передаются в MainWindow.
 *
 * Явная передача вместо обращения к одиночкам делает зависимости каждого виджета
 * видимыми прямо в его конструкторе. Это начинает окупаться, как только появляются
 * панели: по сигнатуре сразу понятно, что панель логирования трогает настройки, а панель
 * поиска — нет.
 *
 * \note Ни один указатель не принадлежит структуре. Все объекты созданы в main() на стеке
 *       и переживают окно.
 */
struct AppContext
{
    SettingsStore *settings = nullptr;     ///< Общие настройки (`settings.json`).
    PluginManager *plugins = nullptr;      ///< Плагины интерфейсов.
    PanelPluginRegistry *panels = nullptr; ///< Плагины панелей и обработки данных.
    InterfaceRegistry *registry = nullptr; ///< Доступные устройства.
    ThemeManager *theme = nullptr;         ///< Оформление и палитра.
    Session *session = nullptr;            ///< Сеанс работы с интерфейсом.
    HistoryStore *history = nullptr;       ///< История строки отправки.
};

} // namespace spotty
