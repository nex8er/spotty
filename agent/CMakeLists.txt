# Агент проброса UART в raw TCP (spotty-uartd).
#
# Здесь он собирается нативно и нужен только тестам: настоящие бинарники под x86_64, i386,
# arm64, armv7/armv6/armel собирает agent/build-agents.sh через `zig cc` и кладёт в
# resources/agent/, откуда Spotty заливает их на удалённый хост. Тесты проверяют тот же
# исходник, что уходит в эти бинарники, — отдельной «тестовой» копии логики нет.
#
# C включается здесь, а не в корневом project(): остальной проект на C++, и нужен язык
# только тем, кто собирает тесты.
enable_language(C)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SPOTTY_UARTD_VERSION LIMIT_COUNT 1)

add_executable(spotty-uartd spotty-uartd.c)
set_target_properties(spotty-uartd PROPERTIES
    C_STANDARD 11
    C_EXTENSIONS ON # cfmakeraw и TIOCEXCL вне строгого режима
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/agent")
target_compile_options(spotty-uartd PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(spotty-uartd PRIVATE
    "SPOTTY_UARTD_VERSION=\"${SPOTTY_UARTD_VERSION}\"")
