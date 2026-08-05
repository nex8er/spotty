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
