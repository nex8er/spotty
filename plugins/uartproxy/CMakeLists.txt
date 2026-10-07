# Перехват последовательного порта, открытого другой программой: Spotty встаёт между
# устройством и этой программой. Зависит от Qt6::SerialPort, как и uart.
#
# PtyPort — POSIX-вариант виртуального порта (pty); на Windows виртуальным концом служит
# уже существующий порт из пары com0com и подобных, поэтому файла там нет вовсе.
set(UARTPROXY_SOURCES
    UartProxyPlugin.cpp
    UartProxyPlugin.h
    UartProxyChannel.cpp
    UartProxyChannel.h
    UartProxyPorts.cpp
    UartProxyPorts.h
    UartProxySettings.h
    VirtualPort.h
    SerialVirtualPort.cpp
    SerialVirtualPort.h
)
if(UNIX)
    list(APPEND UARTPROXY_SOURCES PtyPort.cpp PtyPort.h)
endif()

spotty_add_plugin(spotty-plugin-uartproxy
    CLASS_NAME UartProxyPlugin
    SOURCES ${UARTPROXY_SOURCES}
    LINK Qt6::SerialPort
)
