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

# Отдельный набор для панельного SDK.
#
# Отдельный, потому что он единственный линкуется с Qt6::Widgets: смысл разделения на
# spotty-core и src/ui в том и состоит, что основной набор обходится без них. Слить их
# значило бы потерять эту гарантию — она держится ровно на том, что spotty-tests не видит
# ни одного виджета.
#
# spotty-ui-tests намеренно не участвует в подсчёте покрытия: там мерялась бы доля
# покрытого кода интерфейса, а он проверяется запуском, а не модульными тестами.
add_library(spotty-ui-test-main STATIC
    main.cpp
    support/TestSupport.h
    support/FakeInterfacePlugin.h
    support/FakePanelPlugin.h
)
target_link_libraries(spotty-ui-test-main PUBLIC Spotty::Core Spotty::UiApi
                                                 GTest::gtest GTest::gmock)
target_include_directories(spotty-ui-test-main PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

spotty_add_test(spotty-tests
    SOURCES
        test_ansi_parser.cpp
        test_terminal_buffer.cpp
        test_packetizer.cpp
        test_csv_detector.cpp
        test_data_codec.cpp
        test_history_store.cpp
        test_settings_store.cpp
        test_settings_migration.cpp
        test_app_settings.cpp
        test_highlight_rules.cpp
        test_data_generator.cpp
        test_macro_store.cpp
        test_log_writer.cpp
        test_plot_format.cpp
        test_sample_buffer.cpp
        test_sample_parser.cpp
        test_plot_model.cpp
        test_plot_view.cpp
        test_decimator.cpp
        test_plot_math.cpp
        test_interface_registry.cpp
        test_session.cpp
        test_data_filter.cpp
        test_single_instance.cpp
        test_paths.cpp
    LINK
        spotty-test-main
)

spotty_add_test(spotty-ui-tests
    SOURCES
        test_panel_registry.cpp
        # Реестр живёт в src/ui и входит в исполняемый файл приложения, а не в
        # библиотеку. Проще взять его исходник сюда напрямую, чем заводить библиотеку
        # ради одного класса.
        "${CMAKE_SOURCE_DIR}/src/ui/PanelPluginRegistry.cpp"
    LINK
        spotty-ui-test-main
    # Панель-двойник умеет вернуть настоящий QLabel, и как только этим воспользуется хоть
    # один случай, набору понадобится экран. Внеэкранная платформа снимает вопрос заранее
    # и на машине сборки без графической подсистемы тоже.
    ENVIRONMENT
        QT_QPA_PLATFORM=offscreen
)
target_include_directories(spotty-ui-tests PRIVATE "${CMAKE_SOURCE_DIR}/src/ui")

# Цель `coverage` объявляется после исполняемого файла тестов: инструментам нужен его
# путь, чтобы прочитать карту покрытия. Меряется только spotty-tests.
spotty_add_coverage_target(spotty-tests)

# Замер прогоняет ctest целиком, а тот запускает оба набора. Цифру spotty-ui-tests не
# меняет — отчёт строится по одному бинарнику, — но собран он должен быть, иначе ctest
# сообщит о непостроенном тесте и цель упадёт, ничего не измерив.
if(TARGET coverage)
    add_dependencies(coverage spotty-ui-tests)
endif()
