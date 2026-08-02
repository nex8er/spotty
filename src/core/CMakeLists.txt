# Логика приложения, намеренно не зависящая от Qt6::Widgets.
#
# Это ограничение — смысл разделения, а не формальность. Оно оставляет буфер терминала,
# разборщик ANSI, пакетизатор и настройки проверяемыми без QApplication и делает
# невозможным незаметное прорастание зависимостей от UI в модель. Если чему-то здесь
# понадобился виджет, значит место этому в src/ui.
add_library(spotty-core STATIC
    ChannelWorker.cpp
    ChannelWorker.h
    DataGenerator.cpp
    DataGenerator.h
    HighlightRules.cpp
    HighlightRules.h
    HistoryStore.cpp
    HistoryStore.h
    InterfaceRegistry.cpp
    InterfaceRegistry.h
    LogWriter.cpp
    LogWriter.h
    MacroStore.cpp
    MacroStore.h
    PluginManager.cpp
    PluginManager.h
    Session.cpp
    Session.h
    settings/Paths.cpp
    settings/Paths.h
    settings/SettingsStore.cpp
    settings/SettingsStore.h
    terminal/AnsiParser.cpp
    terminal/AnsiParser.h
    terminal/DataCodec.cpp
    terminal/DataCodec.h
    terminal/Packetizer.cpp
    terminal/Packetizer.h
    terminal/TerminalBuffer.cpp
    terminal/TerminalBuffer.h
    terminal/TextStyle.h
)

add_library(Spotty::Core ALIAS spotty-core)

target_include_directories(spotty-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(spotty-core
    PUBLIC
        Qt6::Core
        Spotty::Api
)

# Версия попадает в код отсюда, а не из отдельного заголовка: единственный источник —
# project(VERSION) в корневом CMakeLists.txt.
target_compile_definitions(spotty-core PUBLIC SPOTTY_VERSION="${SPOTTY_VERSION}")
