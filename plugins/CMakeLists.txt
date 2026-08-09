# Плагины интерфейсов.
#
# Порядок не важен: каждый плагин самодостаточен и объявляется одним вызовом
# spotty_add_plugin() в собственном CMakeLists.txt.
add_subdirectory(loopback)

# UART собирается только при наличии Qt6::SerialPort. Его отсутствие не ломает сборку:
# получится урезанный, но работающий комплект.
if(Qt6SerialPort_FOUND)
    add_subdirectory(uart)
endif()

# J-Link RTT собирается всегда: libjlinkarm грузится в рантайме, а не на этапе сборки, и
# её отсутствие плагин переживает так же, как UART переживает отсутствие Qt6::SerialPort.
add_subdirectory(jlinkrtt)

# Виртуальный источник математических сигналов — отладочный транспорт для csvchart, второй
# после loopback пример "не требует железа".
add_subdirectory(signalgen)

# Плагины панелей и обработки данных. Отличаются от транспортов только KIND PANEL в
# spotty_add_plugin(): им дополнительно достаётся Spotty::UiApi, а с ним Qt6::Widgets.
add_subdirectory(generator)
add_subdirectory(logging)
add_subdirectory(macros)
add_subdirectory(search)

# Плагины-примеры: они и есть проверка API на прочность — писались с нуля, а не
# переделывались из встроенных панелей.
add_subdirectory(csvchart)
add_subdirectory(filesend)
