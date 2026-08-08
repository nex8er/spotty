# Библиотека панельного SDK.
#
# Второй SDK, для плагинов, которые обрабатывают данные и показывают собственный
# интерфейс. Отличается от Spotty::Api ровно одним: ему разрешён Qt6::Widgets.
#
# Правило «spotty-core не линкуется с Widgets» это не нарушает и нарушать не может —
# библиотека стоит сбоку от ядра, а не под ним. Нарушается прежняя формулировка правил 2 и
# 3 («плагины не линкуются с Widgets», «плагин не создаёт виджеты»), и осознанно: они
# относятся к плагинам интерфейсов, где остаются в силе.
#
# Вид библиотеки обязан совпадать с видом Spotty::Api — см. проверку ниже.
if(SPOTTY_STATIC_PLUGINS)
    add_library(spotty-ui-api STATIC)
    target_compile_definitions(spotty-ui-api PUBLIC SPOTTY_UI_API_STATIC)
else()
    add_library(spotty-ui-api SHARED)
endif()

add_library(Spotty::UiApi ALIAS spotty-ui-api)

# Обе библиотеки SDK порождаются из одной опции, поэтому разойтись они не могут. Проверка
# стоит на случай будущей правки: разделяемая Spotty::Api со статической Spotty::UiApi
# даёт плагину собственную копию метаобъектов PanelWidget и IPanelHost, после чего
# connect() и qobject_cast() молча перестают работать. Ошибка сборки лучше, чем панель,
# которая загрузилась и не получает данных.
get_target_property(spotty_api_type spotty-api TYPE)
get_target_property(spotty_ui_api_type spotty-ui-api TYPE)
if(NOT spotty_api_type STREQUAL spotty_ui_api_type)
    message(FATAL_ERROR
        "Spotty::Api is ${spotty_api_type} but Spotty::UiApi is ${spotty_ui_api_type}. "
        "Both SDK libraries must be of the same kind, otherwise Qt meta-objects get "
        "duplicated across the plugin boundary and signal delivery silently breaks.")
endif()

target_sources(spotty-ui-api
    PRIVATE
        IPanelHost.cpp
        PanelWidget.cpp
        # Заголовки перечислены ради AUTOMOC: классы с Q_OBJECT лежат вне каталога цели.
        "${CMAKE_SOURCE_DIR}/include/spotty/ui/IPanelHost.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/ui/IPanelPlugin.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/ui/MdiCodepoints.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/ui/PanelDescriptor.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/ui/PanelWidget.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/ui/SpottyUiApiExport.h"
        "${CMAKE_SOURCE_DIR}/include/spotty/ui/UiApiVersion.h"
)

target_compile_definitions(spotty-ui-api PRIVATE SPOTTY_UI_API_BUILD)

# PUBLIC, а не PRIVATE: заголовки панельного SDK включают spotty/api и spotty/data, и
# плагину нужна ровно та же единственная копия их метаобъектов.
target_link_libraries(spotty-ui-api PUBLIC Spotty::Api Qt6::Widgets)

set_target_properties(spotty-ui-api PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(APPLE)
    set_target_properties(spotty-ui-api PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/spotty.app/Contents/Frameworks"
        INSTALL_NAME_DIR "@rpath"
    )
endif()
