# Корневая сборка Spotty.
#
# Порядок add_subdirectory значим: src/api задаёт цель Spotty::Api, plugins наполняет
# глобальное свойство SPOTTY_STATIC_PLUGIN_CLASSES, а src/ui это свойство читает.
cmake_minimum_required(VERSION 3.21)

project(Spotty
    VERSION 0.1.0
    DESCRIPTION "Modular terminal port monitor"
    HOMEPAGE_URL "https://github.com/nex8er/spotty"
    LANGUAGES CXX)

# project(VERSION) принимает только числа, поэтому буква релиза хранится отдельно.
# Значение попадает в код через определение SPOTTY_VERSION (см. src/core/CMakeLists.txt).
set(SPOTTY_VERSION "${PROJECT_VERSION}a")

option(SPOTTY_STATIC_PLUGINS "Link interface plugins into the executable instead of loading them at runtime" OFF)
option(SPOTTY_BUILD_TESTS "Build unit tests" OFF)
option(SPOTTY_ENABLE_COVERAGE "Instrument spotty-core for coverage analysis" OFF)

if(SPOTTY_ENABLE_COVERAGE AND NOT SPOTTY_BUILD_TESTS)
    # Инструментирование без тестов бессмысленно: измерять было бы нечем.
    message(FATAL_ERROR "SPOTTY_ENABLE_COVERAGE requires SPOTTY_BUILD_TESTS=ON")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)
# SerialPort нужен только плагину UART, LinguistTools — только переводам. Оба
# необязательны: без них собирается урезанный, но работающий комплект.
find_package(Qt6 6.5 QUIET COMPONENTS SerialPort LinguistTools)

qt_standard_project_setup()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT Qt6SerialPort_FOUND)
    message(WARNING "Qt6 SerialPort not found - the UART plugin will not be built.")
endif()

# Загружаемые плагины должны лежать там, где приложение найдёт их относительно своего
# бинарного файла. На macOS это внутренность бандла, на остальных системах — соседний
# каталог plugins. Пути обязаны совпадать с тем, что перечисляет Paths::pluginDirs().
if(APPLE)
    set(SPOTTY_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/spotty.app/Contents/PlugIns/spotty")
else()
    set(SPOTTY_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SpottyPlugin)
include(SpottyCoverage)

add_subdirectory(src/api)
add_subdirectory(src/core)
add_subdirectory(plugins)
add_subdirectory(src/ui)

if(SPOTTY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Spotty ${SPOTTY_VERSION}")
message(STATUS "  Qt              : ${Qt6_VERSION}")
message(STATUS "  Static plugins  : ${SPOTTY_STATIC_PLUGINS}")
message(STATUS "  Tests           : ${SPOTTY_BUILD_TESTS}")
