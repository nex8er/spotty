# Модульные тесты.
#
# Тестируется `spotty-core` — он намеренно не линкуется с Qt6::Widgets именно ради этого:
# всю логику терминала, пакетизации и настроек можно проверить без QApplication и без
# единого виджета на экране.
include(SpottyTesting)
include(GoogleTest)

spotty_provide_googletest()

# Общая точка входа: QCoreApplication нужен для tr(), таймеров и очереди событий, а
# создавать его в каждом наборе значило бы повторять один и тот же код двенадцать раз.
add_library(spotty-test-main STATIC
    main.cpp
    support/TestSupport.h
    support/FakeInterfacePlugin.h
)
target_link_libraries(spotty-test-main PUBLIC Spotty::Core GTest::gtest GTest::gmock)
target_include_directories(spotty-test-main PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

spotty_add_test(spotty-tests
    SOURCES
        test_ansi_parser.cpp
        test_terminal_buffer.cpp
        test_packetizer.cpp
        test_data_codec.cpp
        test_history_store.cpp
        test_settings_store.cpp
        test_app_settings.cpp
        test_highlight_rules.cpp
        test_data_generator.cpp
        test_macro_store.cpp
        test_log_writer.cpp
        test_interface_registry.cpp
        test_session.cpp
        test_single_instance.cpp
        test_paths.cpp
    LINK
        spotty-test-main
)

# Цель `coverage` объявляется после исполняемого файла тестов: инструментам нужен его
# путь, чтобы прочитать карту покрытия.
spotty_add_coverage_target(spotty-tests)
