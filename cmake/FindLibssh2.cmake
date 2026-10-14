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
# Разделяемая библиотека тянет OpenSSL (или mbedTLS, WinCNG) сама — через собственные записи о
# зависимостях. Статическая из конфига пакета — нет: её зависимости добавляются ниже.
if(TARGET Libssh2::libssh2)
    set(Libssh2_FOUND TRUE)
    return()
endif()

# CONFIG и имя строчными: иначе find_package(Libssh2) вернулся бы в этот же файл.
find_package(libssh2 CONFIG QUIET)
foreach(candidate IN ITEMS libssh2::libssh2 libssh2::libssh2_shared libssh2::libssh2_static
                           Libssh2::libssh2_shared Libssh2::libssh2_static)
    if(TARGET ${candidate})
        # Имя libssh2::libssh2 в конфиге — ALIAS (CMake 3.18+), а у псевдонима свойств не поменять:
        # дальше работаем с настоящей целью.
        get_target_property(aliased ${candidate} ALIASED_TARGET)
        if(aliased)
            set(candidate ${aliased})
        endif()
        # Цель из конфига видна только в каталоге, где её нашли; без GLOBAL соседний каталог
        # получил бы ошибку «target not found» на имя с `::`.
        set_property(TARGET ${candidate} PROPERTY IMPORTED_GLOBAL TRUE)
        add_library(Libssh2::libssh2 INTERFACE IMPORTED GLOBAL)
        target_link_libraries(Libssh2::libssh2 INTERFACE ${candidate})

        # Статическая libssh2 своих зависимостей не несёт: конфиг их находит (find_dependency), но
        # в связях цели их нет, и линковка падает на сотне символов OpenSSL и zlib. Добавляем сами,
        # тоже глобально по той же причине.
        get_target_property(libssh2Kind ${candidate} TYPE)
        if(libssh2Kind STREQUAL "STATIC_LIBRARY")
            find_package(OpenSSL QUIET)
            find_package(ZLIB QUIET)
            foreach(dependency IN ITEMS OpenSSL::Crypto ZLIB::ZLIB)
                if(TARGET ${dependency})
                    set_property(TARGET ${dependency} PROPERTY IMPORTED_GLOBAL TRUE)
                    target_link_libraries(Libssh2::libssh2 INTERFACE ${dependency})
                endif()
            endforeach()
            if(WIN32)
                target_link_libraries(Libssh2::libssh2 INTERFACE ws2_32 crypt32 bcrypt user32 advapi32)
            endif()
        endif()
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
    # На Windows библиотека статическая (vcpkg, x64-windows-static-md) и о своих зависимостях не знает.
    # vcpkg кладёт OpenSSL и zlib в тот же каталог lib, что и libssh2.lib, — берём оттуда, а не через
    # FindOpenSSL: на машине сборки бывает и чужой OpenSSL, а смешать два нельзя. Сборка libssh2 на
    # WinCNG обходится без них — тогда рядом ничего нет, и ничего не добавляется.
    if(WIN32)
        get_filename_component(libssh2LibDir "${LIBSSH2_LIBRARY}" DIRECTORY)
        find_library(LIBSSH2_CRYPTO_LIBRARY NAMES libcrypto crypto HINTS "${libssh2LibDir}" NO_DEFAULT_PATH)
        find_library(LIBSSH2_ZLIB_LIBRARY NAMES zlib z HINTS "${libssh2LibDir}" NO_DEFAULT_PATH)
        mark_as_advanced(LIBSSH2_CRYPTO_LIBRARY LIBSSH2_ZLIB_LIBRARY)
        # Ненайденная библиотека — «…-NOTFOUND», и в связи цели она сломала бы конфигурацию.
        set(libssh2Dependencies ws2_32 crypt32 bcrypt user32 advapi32)
        foreach(dependency IN ITEMS LIBSSH2_CRYPTO_LIBRARY LIBSSH2_ZLIB_LIBRARY)
            if(${dependency})
                list(PREPEND libssh2Dependencies "${${dependency}}")
            endif()
        endforeach()
        set_property(TARGET Libssh2::libssh2 APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${libssh2Dependencies})
    endif()
endif()
mark_as_advanced(LIBSSH2_INCLUDE_DIR LIBSSH2_LIBRARY)
