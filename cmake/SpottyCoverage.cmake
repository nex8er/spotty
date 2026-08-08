# Анализ покрытия кода тестами.
#
# Включается опцией SPOTTY_ENABLE_COVERAGE и добавляет цель `coverage`: прогнать тесты,
# собрать отчёт, показать сводку и проверить нижний порог.
#
# Измеряются `spotty-core` и `spotty-api` — вся логика, не зависящая от Qt6::Widgets.
# Смысла мерить покрытие тестов самими собой нет, а UI и плагины модульными тестами не
# покрываются в принципе — они проверяются запуском.
#
# Сборка с инструментированием заметно медленнее и непригодна для работы, поэтому её
# принято держать в отдельном каталоге:
#
#   cmake -S . -B build-cov -G Ninja -DSPOTTY_BUILD_TESTS=ON -DSPOTTY_ENABLE_COVERAGE=ON
#   cmake --build build-cov --target coverage

# Нижний порог покрытия по строкам. Ниже — цель `coverage` завершается с ошибкой.
#
# Порог существует не ради красивой цифры: он ловит момент, когда в ядро добавили код и
# забыли тест. Стопроцентное покрытие целью не ставится — доводить до него обычно значит
# писать тесты на обработчики ошибок файловой системы, которые всё равно не воспроизвести.
set(SPOTTY_COVERAGE_MINIMUM "80.0" CACHE STRING "Minimum line coverage percentage for the core and the SDK")

function(spotty_enable_coverage target)
    if(NOT SPOTTY_ENABLE_COVERAGE)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # PUBLIC, а не PRIVATE: флаг компоновки нужен и тем, кто линкует эту библиотеку,
        # иначе среда выполнения профилировщика не попадёт в исполняемый файл тестов.
        target_compile_options(${target} PUBLIC -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(${target} PUBLIC -fprofile-instr-generate)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PUBLIC --coverage)
        target_link_options(${target} PUBLIC --coverage)
    else()
        message(WARNING "Coverage is supported for Clang and GCC only; "
                        "SPOTTY_ENABLE_COVERAGE has no effect with ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()

# spotty_add_coverage_target(<цель-с-тестами>)
#
# Создаёт цель `coverage`. Вызывается после того, как объявлен исполняемый файл тестов:
# его путь нужен инструментам для чтения карты покрытия.
function(spotty_add_coverage_target test_target)
    if(NOT SPOTTY_ENABLE_COVERAGE)
        return()
    endif()

    set(coverage_dir "${CMAKE_BINARY_DIR}/coverage")

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        _spotty_coverage_llvm(${test_target} "${coverage_dir}")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        _spotty_coverage_gcc(${test_target} "${coverage_dir}")
    else()
        return()
    endif()

    add_dependencies(coverage ${test_target})
endfunction()

# --- Clang: llvm-profdata + llvm-cov ---------------------------------------------------

function(_spotty_coverage_llvm test_target coverage_dir)
    # Инструменты берутся из того же набора, что и компилятор: llvm-cov из другой версии
    # LLVM не прочитает карту покрытия и выдаст невнятную ошибку про формат.
    get_filename_component(toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(LLVM_PROFDATA llvm-profdata HINTS "${toolchain_bin}")
    find_program(LLVM_COV llvm-cov HINTS "${toolchain_bin}")

    if(NOT LLVM_PROFDATA OR NOT LLVM_COV)
        # На macOS они лежат внутри Xcode и в PATH обычно не видны.
        find_program(XCRUN xcrun)
        if(XCRUN)
            execute_process(COMMAND "${XCRUN}" --find llvm-profdata
                            OUTPUT_VARIABLE LLVM_PROFDATA
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            execute_process(COMMAND "${XCRUN}" --find llvm-cov
                            OUTPUT_VARIABLE LLVM_COV
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        endif()
    endif()

    if(NOT LLVM_PROFDATA OR NOT LLVM_COV)
        message(WARNING "llvm-profdata / llvm-cov not found - coverage target unavailable")
        return()
    endif()

    set(script "${CMAKE_BINARY_DIR}/spotty-coverage.sh")

    file(GENERATE OUTPUT "${script}" CONTENT "#!/usr/bin/env bash
set -euo pipefail

coverage_dir='${coverage_dir}'
binary='$<TARGET_FILE:${test_target}>'

rm -rf \"\$coverage_dir\"
mkdir -p \"\$coverage_dir\"

# %p в имени разводит профили по идентификатору процесса: ctest запускает каждый тест
# отдельным процессом, и без этого они писали бы в один файл, затирая друг друга.
export LLVM_PROFILE_FILE=\"\$coverage_dir/%p.profraw\"
ctest --test-dir '${CMAKE_BINARY_DIR}' --output-on-failure

'${LLVM_PROFDATA}' merge -sparse \"\$coverage_dir\"/*.profraw -o \"\$coverage_dir/merged.profdata\"

# Из отчёта исключается всё, что не является измеряемым кодом: сами тесты, GoogleTest,
# порождённые moc файлы и системные заголовки. Иначе цифра размывается до бессмысленной.
ignore='(/tests/|/src/ui/|/plugins/|_deps/|/moc_|/mocs_compilation|qrc_|/usr/|/Applications/|\\.framework/)'

'${LLVM_COV}' report \"\$binary\" \\
    -instr-profile=\"\$coverage_dir/merged.profdata\" \\
    -ignore-filename-regex=\"\$ignore\"

'${LLVM_COV}' show \"\$binary\" \\
    -instr-profile=\"\$coverage_dir/merged.profdata\" \\
    -ignore-filename-regex=\"\$ignore\" \\
    -format=html -output-dir=\"\$coverage_dir/html\" \\
    -show-line-counts-or-regions >/dev/null

'${LLVM_COV}' export \"\$binary\" \\
    -instr-profile=\"\$coverage_dir/merged.profdata\" \\
    -ignore-filename-regex=\"\$ignore\" \\
    -summary-only > \"\$coverage_dir/summary.json\"

echo
echo \"HTML report: \$coverage_dir/html/index.html\"

python3 '${CMAKE_SOURCE_DIR}/tools/check_coverage.py' \\
    \"\$coverage_dir/summary.json\" '${SPOTTY_COVERAGE_MINIMUM}'
")

    add_custom_target(coverage
        COMMAND chmod +x "${script}"
        COMMAND "${script}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Running tests and measuring coverage"
        USES_TERMINAL
        VERBATIM
    )
endfunction()

# --- GCC: gcovr -------------------------------------------------------------------------

function(_spotty_coverage_gcc test_target coverage_dir)
    find_program(GCOVR gcovr)
    if(NOT GCOVR)
        message(WARNING "gcovr not found - coverage target unavailable")
        return()
    endif()

    add_custom_target(coverage
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${coverage_dir}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${coverage_dir}/html"
        COMMAND ctest --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure
        COMMAND "${GCOVR}"
                --root "${CMAKE_SOURCE_DIR}"
                # Измеряется логика — по той же причине, что и в ветке Clang. Она лежит в
                # двух местах: ядро и SDK, куда переехали MacroStore, LogWriter,
                # DataGenerator, HighlightRules и DataCodec. UI и плагины сюда не входят.
                --filter "${CMAKE_SOURCE_DIR}/src/core"
                --filter "${CMAKE_SOURCE_DIR}/src/api"
                --filter "${CMAKE_SOURCE_DIR}/include/spotty"
                --exclude ".*/moc_.*"
                --exclude ".*/mocs_compilation.*"
                --print-summary
                --html-details "${coverage_dir}/html/index.html"
                --fail-under-line "${SPOTTY_COVERAGE_MINIMUM}"
                "${CMAKE_BINARY_DIR}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Running tests and measuring coverage"
        USES_TERMINAL
        VERBATIM
    )
endfunction()
