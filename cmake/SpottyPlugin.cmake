# spotty_add_plugin(<цель>
#     CLASS_NAME <класс, реализующий IInterfacePlugin или IPanelPlugin>
#     SOURCES    <файлы...>
#     [KIND      INTERFACE | PANEL]
#     [LINK      <дополнительные библиотеки...>])
#
# Один вызов — всё, что должно быть в CMakeLists.txt плагина.
#
# По умолчанию плагин собирается загружаемым модулем и кладётся в SPOTTY_PLUGIN_OUTPUT_DIR,
# где его находит PluginManager. При SPOTTY_STATIC_PLUGINS=ON он становится статической
# библиотекой, а имя его класса записывается в глобальное свойство
# SPOTTY_STATIC_PLUGIN_CLASSES, откуда src/ui порождает нужные объявления Q_IMPORT_PLUGIN.
# Роль плагина при этом не важна: Q_IMPORT_PLUGIN их не различает, и оба вида идут в один
# список.
#
# KIND INTERFACE (умолчание) — транспорт. Линкуется только со Spotty::Api: никогда со
# spotty-core и никогда с Qt6::Widgets. Это удерживает SDK честным — если плагину чего-то
# не хватает, значит этого не хватает в публичном API, и добавлять нужно туда.
#
# KIND PANEL — панель и обработка данных. Дополнительно получает Spotty::UiApi, а с ним и
# Qt6::Widgets: такой плагин показывает собственный интерфейс, и запрет на виджеты к нему
# не относится. Запрет на spotty-core остаётся в силе и для него.
function(spotty_add_plugin target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "CLASS_NAME;KIND" "SOURCES;LINK")

    if(NOT ARG_CLASS_NAME)
        message(FATAL_ERROR "spotty_add_plugin(${target}): CLASS_NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "spotty_add_plugin(${target}): SOURCES is required")
    endif()

    # Умолчание — транспорт: их больше, и они появились первыми.
    if(NOT ARG_KIND)
        set(ARG_KIND INTERFACE)
    endif()
    if(NOT ARG_KIND STREQUAL "INTERFACE" AND NOT ARG_KIND STREQUAL "PANEL")
        message(FATAL_ERROR
            "spotty_add_plugin(${target}): KIND must be INTERFACE or PANEL, got ${ARG_KIND}")
    endif()

    if(SPOTTY_STATIC_PLUGINS)
        qt_add_plugin(${target} STATIC CLASS_NAME ${ARG_CLASS_NAME})
        set_property(GLOBAL APPEND PROPERTY SPOTTY_STATIC_PLUGIN_CLASSES ${ARG_CLASS_NAME})
        set_property(GLOBAL APPEND PROPERTY SPOTTY_STATIC_PLUGIN_TARGETS ${target})
    else()
        # SHARED здесь означает MODULE: qt_add_plugin создаёт именно загружаемый модуль.
        # Указано явно, чтобы результат не зависел от того, как собран сам Qt.
        qt_add_plugin(${target} SHARED CLASS_NAME ${ARG_CLASS_NAME})
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${SPOTTY_PLUGIN_OUTPUT_DIR}")
    endif()

    target_sources(${target} PRIVATE ${ARG_SOURCES})

    if(ARG_KIND STREQUAL "PANEL")
        target_link_libraries(${target} PRIVATE Spotty::UiApi ${ARG_LINK})
    else()
        target_link_libraries(${target} PRIVATE Spotty::Api ${ARG_LINK})
    endif()
endfunction()
