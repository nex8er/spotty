# Подключение GoogleTest.
#
# Сначала пробуем системный пакет: в дистрибутивах и в контейнерах сборки он часто уже
# стоит, и тогда не нужно ни сети, ни лишней компиляции. Если не нашли — скачиваем
# закреплённую версию.
#
# Версия закреплена тегом, а не веткой: сборка, которая тянет «последнее» из сети, рано
# или поздно ломается сама по себе, и виноватым выглядит последний коммит.
set(SPOTTY_GTEST_TAG "v1.15.2" CACHE STRING "GoogleTest version to fetch when not found")

function(spotty_provide_googletest)
    if(TARGET GTest::gtest_main)
        return()
    endif()

    find_package(GTest QUIET)
    if(GTest_FOUND)
        message(STATUS "GoogleTest: using system package")
        return()
    endif()

    message(STATUS "GoogleTest: fetching ${SPOTTY_GTEST_TAG}")

    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG "${SPOTTY_GTEST_TAG}"
        GIT_SHALLOW TRUE
        # Не даём GoogleTest навязывать проекту свои настройки сборки.
        OVERRIDE_FIND_PACKAGE
    )

    # На Windows GoogleTest по умолчанию линкует свою среду выполнения статически, а Qt —
    # динамически. Смешение этих двух режимов даёт ошибки компоновки, разобрать которые
    # заметно труднее, чем поставить этот флаг.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    # Заголовки GoogleTest помечаются системными, иначе его собственные предупреждения
    # (в C++20 — преобразование char8_t в char32_t) сыплются при каждой сборке. Чужой шум
    # в выводе опаснее, чем кажется: он приучает не читать предупреждения вовсе, и в нём
    # тонут свои.
    #
    # Через свойство, а не через SYSTEM в FetchContent_Declare: тот появился только в
    # CMake 3.25, а проект держит планку 3.21.
    foreach(gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
        if(NOT TARGET ${gtest_target})
            continue()
        endif()

        get_target_property(includes ${gtest_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(includes)
            set_target_properties(${gtest_target} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${includes}")
        endif()

        # Системные заголовки глушат предупреждения только у потребителей; сам GoogleTest
        # при сборке под C++20 всё равно ругается на преобразование char8_t в char32_t в
        # собственном коде. Флаг только для Clang: другие компиляторы сочли бы неизвестный
        # параметр ошибкой и сломали бы сборку там, где чинить нечего.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${gtest_target} PRIVATE -Wno-character-conversion)
        endif()
    endforeach()
endfunction()

# spotty_add_test(<имя> SOURCES <файлы...> [LINK <библиотеки...>])
#
# Один вызов на группу тестов. gtest_discover_tests регистрирует каждый TEST отдельно,
# поэтому ctest показывает, какой именно случай упал, а не только «набор не прошёл».
#
# Библиотеки Qt, ядра и GoogleTest сюда не добавляются: их приносит переданная в LINK
# общая цель. Повторное указание даёт предупреждения компоновщика о дублирующихся
# библиотеках — безобидные, но зашумляющие вывод сборки, из-за чего в нём теряются
# настоящие.
function(spotty_add_test target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LINK")

    add_executable(${target} ${ARG_SOURCES})

    target_link_libraries(${target} PRIVATE ${ARG_LINK})

    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

    gtest_discover_tests(${target}
        # Тесты трогают файловую систему во временных каталогах; запуск из каталога сборки
        # делает пути в сообщениях об ошибках предсказуемыми.
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        DISCOVERY_TIMEOUT 30
    )
endfunction()
