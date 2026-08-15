# Библиотека SDK плагинов.
#
# Содержит две вещи: контракты (include/spotty/api) и готовую логику, которой плагин
# вправе пользоваться (include/spotty/data). Граница между ними и остальной программой
# проходит по Qt6::Widgets: всё здешнее обязано оставаться проверяемым без QApplication.
#
# Библиотека нужна ради QObject'ов в API (IInterfaceChannel, LogWriter), чтобы их
# метаобъекты, таблицы виртуальных функций и typeinfo имели ровно одно определение, общее
# для исполняемого файла и всех плагинов.
#
# Разделяемая по умолчанию, статическая при вкомпилированных плагинах: там один двоичный
# файл и делить между модулями нечего.
#
# Подробное объяснение, почему разделяемость критична, — в include/spotty/api/SpottyApiExport.h.
if(SPOTTY_STATIC_PLUGINS)
    add_library(spotty-api STATIC)
    target_compile_definitions(spotty-api PUBLIC SPOTTY_API_STATIC)
else()
    add_library(spotty-api SHARED)
endif()

add_library(Spotty::Api ALIAS spotty-api)

target_sources(spotty-api
    PRIVATE
        CsvDetector.cpp
        DataCodec.cpp
        DataGenerator.cpp
        FileUtils.cpp
        Formatting.cpp
        HighlightRules.cpp
        IInterfaceChannel.cpp
        LogWriter.cpp
        MacroStore.cpp
        PlotFormat.cpp
        # Заголовки перечислены, чтобы AUTOMOC обработал классы с Q_OBJECT: они лежат вне
        # каталога цели, и без явного указания moc до них не доберётся.
        "${CMAKE_SOURCE_DIR}/include/spotty/api/IInterfaceChannel.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/IInterfacePlugin.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/ApiVersion.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/ChannelState.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/DataDirection.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/IDataFilter.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/InterfaceDescriptor.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/SettingsSchema.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/SpottyApiExport.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/data/CsvDetector.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/data/DataCodec.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/data/DataGenerator.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/data/FileUtils.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/data/Formatting.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/data/HighlightRules.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/data/LogWriter.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/data/MacroStore.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/data/PlotFormat.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/data/TextStyle.h"
)

target_compile_definitions(spotty-api PRIVATE SPOTTY_API_BUILD)

target_include_directories(spotty-api PUBLIC "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(spotty-api PUBLIC Qt6::Core)

set_target_properties(spotty-api PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    # Скрываем всё, что не помечено к экспорту: поверхность SDK становится ровно такой,
    # какую заявляют заголовки, на всех платформах, а не только в Windows.
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(APPLE)
    # И плагины, и исполняемый файл подхватывают библиотеку изнутри бандла.
    set_target_properties(spotty-api PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/spotty.app/Contents/Frameworks"
        INSTALL_NAME_DIR "@rpath"
    )
endif()

# Логика, которую покрывают тесты, живёт теперь и здесь: MacroStore, LogWriter,
# DataGenerator, HighlightRules и DataCodec переехали сюда из ядра, чтобы плагины имели на
# них право. Мерить покрытие только по spotty-core после этого значило бы вынести из
# знаменателя 79 тестов и молча поднять планку для оставшегося кода.
spotty_enable_coverage(spotty-api)
