# Корневая сборка Spotty.
#
# Порядок add_subdirectory значим: src/api задаёт цель Spotty::Api, src/uiapi — надстроенную
# над ней Spotty::UiApi, plugins наполняет глобальное свойство SPOTTY_STATIC_PLUGIN_CLASSES,
# а src/ui это свойство читает.
cmake_minimum_required(VERSION 3.21)

# На macOS без явного sysroot компилятор берёт SDK из Command Line Tools, а линкер — из
# выбранного Xcode. Когда CLT новее Xcode (а это бывает после обновления только одного из
# двух), линкер отвечает `unknown architecture` на `.tbd` чужого SDK, и сборка падает на
# самом последнем шаге. Имя `macosx` разрешается через `xcrun` в SDK выбранного Xcode, а не
# в пути, зашитом в проект. Явный SDKROOT пользователя этим не перекрывается.
#
# До project(): после него CMAKE_OSX_SYSROOT уже определён, и значение не подхватится.
if(APPLE AND NOT CMAKE_OSX_SYSROOT AND NOT DEFINED ENV{SDKROOT})
    # FORCE: в каталогах, настроенных раньше, в кэше уже лежит пустое значение, и без него
    # set(CACHE) его не заменит — старая сборка продолжала бы падать.
    set(CMAKE_OSX_SYSROOT macosx CACHE STRING "macOS SDK name or path" FORCE)
endif()

project(Spotty
    VERSION 0.8.0
    DESCRIPTION "Modular terminal port monitor"
    HOMEPAGE_URL "https://github.com/nex8er/spotty"
    LANGUAGES CXX)

# Значение попадает в код через определение SPOTTY_VERSION (см. src/core/CMakeLists.txt) и в
# имена пакетов. Раньше к номеру дописывалась буква «a» (альфа); теперь версия — просто
# число, и тег релиза выглядит как `v0.8.0`.
set(SPOTTY_VERSION "${PROJECT_VERSION}")

option(SPOTTY_STATIC_PLUGINS "Link interface plugins into the executable instead of loading them at runtime" OFF)
option(SPOTTY_BUILD_TESTS "Build unit tests" OFF)
option(SPOTTY_ENABLE_COVERAGE "Instrument the core and the SDK for coverage analysis" OFF)

if(SPOTTY_ENABLE_COVERAGE AND NOT SPOTTY_BUILD_TESTS)
    # Инструментирование без тестов бессмысленно: измерять было бы нечем.
    message(FATAL_ERROR "SPOTTY_ENABLE_COVERAGE requires SPOTTY_BUILD_TESTS=ON")
endif()

if(SPOTTY_ENABLE_COVERAGE AND NOT SPOTTY_STATIC_PLUGINS)
    # Измеряемая логика теперь и в spotty-api, а он по умолчанию разделяемый. Счётчики
    # покрытия в разделяемой библиотеке ведут собственный набор данных профилирования, и
    # сводка по исполняемому файлу тестов их не видит. В статической сборке делить между
    # модулями нечего, и обе библиотеки становятся STATIC по общему правилу.
    set(SPOTTY_STATIC_PLUGINS ON CACHE BOOL "" FORCE)
    message(STATUS "Coverage build implies SPOTTY_STATIC_PLUGINS=ON")
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
add_subdirectory(src/uiapi)
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
