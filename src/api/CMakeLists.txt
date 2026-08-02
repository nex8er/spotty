# Библиотека SDK плагинов.
#
# Почти целиком состоит из заголовков; библиотека нужна ради единственного QObject в API
# (IInterfaceChannel), чтобы его метаобъект, таблица виртуальных функций и typeinfo имели
# ровно одно определение, общее для исполняемого файла и всех плагинов.
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
        IInterfaceChannel.cpp
        # Заголовки перечислены, чтобы AUTOMOC обработал класс с Q_OBJECT: он лежит вне
        # каталога цели, и без явного указания moc до него не доберётся.
        "${CMAKE_SOURCE_DIR}/include/spotty/api/IInterfaceChannel.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/IInterfacePlugin.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/ApiVersion.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/ChannelState.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/InterfaceDescriptor.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/SettingsSchema.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/api/SpottyApiExport.h"
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
