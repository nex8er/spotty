# Цель spotty-package: собрать распространяемый комплект для текущей платформы.
#
# Каждая система требует своего: macOS — сложить библиотеки Qt внутрь бандла, Windows —
# рядом с исполняемым файлом, Linux — образ AppImage. Общего механизма у Qt для этого нет,
# поэтому здесь три ветки, а не одна.
#
# Статический вариант проще распространять, но динамический тоже является полноценным
# комплектом: его модули и их зависимости раскладываются рядом с приложением.

set(SPOTTY_PACKAGE_DIR "${CMAKE_BINARY_DIR}/package")

function(spotty_setup_packaging target)
    get_property(spotty_dynamic_plugins GLOBAL PROPERTY SPOTTY_DYNAMIC_PLUGIN_TARGETS)

    add_custom_target(spotty-package
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPOTTY_PACKAGE_DIR}"
        COMMENT "Packaging Spotty ${SPOTTY_VERSION}"
        VERBATIM
    )
    add_dependencies(spotty-package ${target} ${spotty_dynamic_plugins})

    if(APPLE)
        _spotty_package_macos(${target} ${spotty_dynamic_plugins})
    elseif(WIN32)
        _spotty_package_windows(${target} ${spotty_dynamic_plugins})
    elseif(UNIX)
        _spotty_package_linux(${target} ${spotty_dynamic_plugins})
    endif()
endfunction()

# --- macOS: сложить Qt внутрь бандла и упаковать в образ диска -------------------------

function(_spotty_package_macos target)
    find_program(MACDEPLOYQT macdeployqt HINTS "${QT6_INSTALL_PREFIX}/bin"
                 "${_qt_bin_dir}" "$ENV{QTDIR}/bin")
    if(NOT MACDEPLOYQT)
        message(WARNING "macdeployqt not found - spotty-package will not bundle Qt")
        return()
    endif()

    set(bundle "$<TARGET_BUNDLE_DIR:${target}>")
    set(dmg "${SPOTTY_PACKAGE_DIR}/Spotty-${SPOTTY_VERSION}-macOS-${CMAKE_SYSTEM_PROCESSOR}.dmg")
    set(extra_executables)
    set(extra_library_paths)

    # QtSvg в Homebrew поставляется отдельно от qtbase. Один из штатных плагинов Qt,
    # который macdeployqt переносит в бандл, ссылается на этот фреймворк, хотя Spotty
    # напрямую с ним не линкуется. Без явного пути упаковка динамической конфигурации
    # падает ещё до создания dmg.
    find_package(Qt6 QUIET COMPONENTS Svg)
    if(Qt6Svg_FOUND)
        get_filename_component(qt_svg_lib_dir "${Qt6Svg_DIR}/../.." ABSOLUTE)
        list(APPEND extra_library_paths "-libpath=${qt_svg_lib_dir}")
    endif()

    foreach(plugin IN LISTS ARGN)
        # В динамической сборке модуль уже лежит внутри бандла. Указываем его явно,
        # чтобы macdeployqt переписал пути к тем же фреймворкам Qt и в его Mach-O.
        list(APPEND extra_executables "-executable=$<TARGET_FILE:${plugin}>")
    endforeach()

    # Шаги разделены намеренно, а не свёрнуты в «macdeployqt -dmg».
    #
    # macdeployqt переписывает пути к библиотекам внутри скопированных .dylib, и этим
    # ломает их подписи. На arm64 подпись обязательна: система убивает процесс с
    # неверной подписью сигналом KILL ещё до main(), и приложение просто не запускается,
    # ничего не сообщая. Поэтому после развёртывания бандл подписывается заново, и только
    # потом собирается образ.
    add_custom_command(TARGET spotty-package POST_BUILD
        COMMAND "${MACDEPLOYQT}" "${bundle}" ${extra_library_paths} ${extra_executables}
        # Ad-hoc подпись («-») достаточна для запуска на своей машине. Для раздачи другим
        # нужен Developer ID и нотаризация — это уже вопрос учётной записи, а не сборки.
        COMMAND codesign --force --deep --sign - "${bundle}"
        COMMAND codesign --verify --deep "${bundle}"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${dmg}"
        COMMAND hdiutil create -volname "Spotty" -srcfolder "${bundle}"
                -ov -format UDZO -quiet "${dmg}"
        COMMENT "macdeployqt, codesign, dmg"
        VERBATIM
    )
endfunction()

# --- Windows: положить библиотеки Qt рядом и сжать каталог -----------------------------

function(_spotty_package_windows target)
    find_program(WINDEPLOYQT windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin"
                 "${_qt_bin_dir}" "$ENV{QTDIR}/bin")
    if(NOT WINDEPLOYQT)
        message(WARNING "windeployqt not found - spotty-package will not bundle Qt")
        return()
    endif()

    set(staging_root "${CMAKE_BINARY_DIR}/staging")
    set(staging "${staging_root}/Spotty")

    # --no-compiler-runtime верен для MSVC (CI собирает win64_msvc2022_64): на целевой
    # машине почти всегда уже стоит редистрибутив, и тащить его копию незачем. Для MinGW
    # это предположение неверно — libgcc/libstdc++/libwinpthread в Windows не входят и
    # никаким установщиком заранее не кладутся, так что без них exe не запустится вообще,
    # с тем же эффектом, что отсутствующие Qt6*.dll. windeployqt сам находит и копирует
    # эти три DLL рядом с исполняемым файлом, если флаг не мешает.
    set(_deploy_args --release)
    if(NOT MINGW)
        list(APPEND _deploy_args --no-compiler-runtime)
    endif()

    add_custom_command(TARGET spotty-package POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${staging}"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${target}>" "${staging}"
        COMMAND "${CMAKE_COMMAND}" -E copy -t "${staging}" $<TARGET_RUNTIME_DLLS:${target}>
        # --no-translations здесь неуместно: переводы уже встроены в ресурсы, но Qt несёт
        # свои собственные, и без них диалоги окажутся наполовину английскими.
        COMMAND "${WINDEPLOYQT}" ${_deploy_args}
                "${staging}/$<TARGET_FILE_NAME:${target}>"
        COMMENT "windeployqt"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    foreach(plugin IN LISTS ARGN)
        # Плагины Spotty не распознаются windeployqt по главному exe: они подгружаются
        # QPluginLoader уже после старта. Поэтому копируем каждый модуль и просим Qt
        # развернуть его зависимости в тот же корень комплекта.
        add_custom_command(TARGET spotty-package POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${staging}/plugins"
            COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${plugin}>" "${staging}/plugins"
            COMMAND "${CMAKE_COMMAND}" -E copy -t "${staging}" $<TARGET_RUNTIME_DLLS:${plugin}>
            COMMAND "${WINDEPLOYQT}" ${_deploy_args} --dir "${staging}"
                    "$<TARGET_FILE:${plugin}>"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )
    endforeach()

    # Отдельная команда, а не COMMAND в цепочке выше: WORKING_DIRECTORY меняет рабочий
    # каталог для всей цепочки сразу, включая самую первую её команду — а staging_root в
    # первый раз ещё не существует, пока make_directory из предыдущего шага его не создаст.
    # cd в несуществующий каталог валит сборку раньше, чем что-либо успевает отработать.
    # Несколько POST_BUILD у одной цели выполняются по порядку добавления, так что второй
    # шаг гарантированно видит уже созданный staging.
    add_custom_command(TARGET spotty-package POST_BUILD
        # cmake -E tar кладёт в архив путь ровно таким, каким его посчитал от рабочего
        # каталога. Абсолютный ${staging} давал вход вида "../../staging/Spotty/..." — запись
        # с выходом за пределы архива, которую Проводник Windows молча не показывает: архив
        # выглядит пустым, хотя байты в нём есть. Рабочий каталог — родитель staging, путь —
        # просто "Spotty", и в архиве оказывается чистое "Spotty/spotty.exe" без "..".
        COMMAND "${CMAKE_COMMAND}" -E tar cf
                "${SPOTTY_PACKAGE_DIR}/Spotty-${SPOTTY_VERSION}-Windows-x64.zip"
                --format=zip "Spotty"
        WORKING_DIRECTORY "${staging_root}"
        COMMENT "zip"
        VERBATIM
    )
endfunction()

# --- Linux: образ AppImage --------------------------------------------------------------

function(_spotty_package_linux target)
    set(plugin_copy)
    foreach(plugin IN LISTS ARGN)
        string(APPEND plugin_copy
            "cp '$<TARGET_FILE:${plugin}>' \"\\$appdir/usr/bin/plugins/\"\n"
            "plugin_args+=(--executable \"\\$appdir/usr/bin/plugins/$<TARGET_FILE_NAME:${plugin}>\")\n")
    endforeach()

    # linuxdeploy не входит ни в один дистрибутив, поэтому скачивается по месту. Наличие
    # проверяется во время сборки, а не настройки: на машине разработчика его обычно нет,
    # и требовать его при каждом cmake было бы навязчиво.
    #
    # Имя двоичного файла берётся из CMAKE_SYSTEM_PROCESSOR, а не захардкожено под x86_64:
    # linuxdeploy публикует отдельные сборки на архитектуру ("x86_64", "aarch64"), и то же
    # значение совпадает с uname -m на Linux, так что подставляется без пересчёта.
    set(script "${CMAKE_BINARY_DIR}/spotty-appimage.sh")

    file(GENERATE OUTPUT "${script}" CONTENT "#!/usr/bin/env bash
set -euo pipefail

package_dir='${SPOTTY_PACKAGE_DIR}'
appdir=\"${CMAKE_BINARY_DIR}/AppDir\"
binary='$<TARGET_FILE:${target}>'
arch='${CMAKE_SYSTEM_PROCESSOR}'
plugin_args=()

if ! command -v \"linuxdeploy-\$arch.AppImage\" >/dev/null 2>&1; then
    echo \"downloading linuxdeploy for \$arch\"
    curl -sSfLo \"${CMAKE_BINARY_DIR}/linuxdeploy\" \\
        \"https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-\$arch.AppImage\"
    curl -sSfLo \"${CMAKE_BINARY_DIR}/linuxdeploy-plugin-qt\" \\
        \"https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-\$arch.AppImage\"
    chmod +x \"${CMAKE_BINARY_DIR}/linuxdeploy\" \"${CMAKE_BINARY_DIR}/linuxdeploy-plugin-qt\"
fi

rm -rf \"\$appdir\"
mkdir -p \"\$appdir/usr/bin\" \"\$appdir/usr/bin/plugins\" \"\$appdir/usr/share/applications\"
cp \"\$binary\" \"\$appdir/usr/bin/\"
${plugin_copy}

cat > \"\$appdir/usr/share/applications/spotty.desktop\" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Spotty
Comment=Modular terminal port monitor
Exec=spotty
Icon=spotty
Categories=Development;Electronics;
Terminal=false
DESKTOP

export PATH=\"${CMAKE_BINARY_DIR}:\$PATH\"
export OUTPUT=\"\$package_dir/Spotty-${SPOTTY_VERSION}-Linux-\$arch.AppImage\"
# И linuxdeploy, и его плагин Qt сами по себе AppImage и по умолчанию монтируют себя через
# FUSE — а в контейнере CI /dev/fuse обычно недоступен без лишних привилегий. С этой
# переменной оба вместо монтирования распаковываются во временный каталог и запускаются
# оттуда напрямую.
export APPIMAGE_EXTRACT_AND_RUN=1
linuxdeploy --appdir \"\$appdir\" --plugin qt \"\${plugin_args[@]}\" \\
    --icon-file '${CMAKE_SOURCE_DIR}/resources/icons/spotty-256.png' \\
    --icon-filename spotty \\
    --output appimage
")

    add_custom_command(TARGET spotty-package POST_BUILD
        COMMAND chmod +x "${script}"
        COMMAND "${script}"
        COMMENT "linuxdeploy + AppImage"
        VERBATIM
    )
endfunction()
