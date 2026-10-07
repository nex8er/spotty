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
set(UARTPROXY_LINK Qt6::SerialPort)
if(UNIX)
    list(APPEND UARTPROXY_SOURCES PtyPort.cpp PtyPort.h)
endif()
# Com0com — обнаружение драйвера пар портов и запуск его настройки: на Windows без него
# перехватчику не с чем работать, и диалог обязан об этом сказать. Реестр и запуск с
# повышением прав — advapi32 и shell32.
if(WIN32)
    list(APPEND UARTPROXY_SOURCES Com0com.cpp Com0com.h)
    list(APPEND UARTPROXY_LINK advapi32 shell32)
endif()

spotty_add_plugin(spotty-plugin-uartproxy
    CLASS_NAME UartProxyPlugin
    SOURCES ${UARTPROXY_SOURCES}
    LINK ${UARTPROXY_LINK}
)
