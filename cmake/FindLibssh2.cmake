# Поиск libssh2: цель Libssh2::libssh2.
#
# Сначала настоящий конфиг пакета (vcpkg, сборка из исходников апстрима), затем ручной поиск
# заголовка и библиотеки. Ручной нужен там, где конфига нет: у Homebrew и у пакетов
# libssh2-1-dev в Debian есть только pkg-config-файл, а самого pkg-config на машине сборки
# может не быть (его нет и у этого проекта в зависимостях).
#
# Имена в конфиге пакета пишутся строчными: пакет `libssh2`, цель `libssh2::libssh2` (так у
# апстрима и у vcpkg). Проекту нужно одно стабильное имя, поэтому найденная цель оборачивается в
# `Libssh2::libssh2` — глобальную, чтобы её видели и плагин, и тесты из соседних каталогов.
#
# Найденная библиотека тянет OpenSSL (или mbedTLS, WinCNG) сама — через собственные записи о
# зависимостях; проекту знать об этом не нужно.
if(TARGET Libssh2::libssh2)
    set(Libssh2_FOUND TRUE)
    return()
endif()

# CONFIG и имя строчными: иначе find_package(Libssh2) вернулся бы в этот же файл.
find_package(libssh2 CONFIG QUIET)
foreach(candidate IN ITEMS libssh2::libssh2 libssh2::libssh2_shared libssh2::libssh2_static
                           Libssh2::libssh2_shared Libssh2::libssh2_static)
    if(TARGET ${candidate})
        # Цель из конфига видна только в каталоге, где её нашли; без GLOBAL соседний каталог
        # получил бы ошибку «target not found» на имя с `::`.
        set_property(TARGET ${candidate} PROPERTY IMPORTED_GLOBAL TRUE)
        add_library(Libssh2::libssh2 INTERFACE IMPORTED GLOBAL)
        target_link_libraries(Libssh2::libssh2 INTERFACE ${candidate})
        set(Libssh2_FOUND TRUE)
        return()
    endif()
endforeach()

find_path(LIBSSH2_INCLUDE_DIR libssh2.h)
find_library(LIBSSH2_LIBRARY NAMES ssh2 libssh2)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libssh2 REQUIRED_VARS LIBSSH2_LIBRARY LIBSSH2_INCLUDE_DIR)

if(Libssh2_FOUND)
    add_library(Libssh2::libssh2 UNKNOWN IMPORTED GLOBAL)
    set_target_properties(Libssh2::libssh2 PROPERTIES
        IMPORTED_LOCATION "${LIBSSH2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBSSH2_INCLUDE_DIR}")
    # На Windows libssh2 требует Winsock; импортированная библиотека о нём не знает.
    if(WIN32)
        set_property(TARGET Libssh2::libssh2 APPEND PROPERTY INTERFACE_LINK_LIBRARIES ws2_32 crypt32)
    endif()
endif()
mark_as_advanced(LIBSSH2_INCLUDE_DIR LIBSSH2_LIBRARY)
