# Плагин последовательных портов. Единственная внешняя зависимость — Qt6::SerialPort.
spotty_add_plugin(spotty-plugin-uart
    CLASS_NAME UartPlugin
    SOURCES
        UartPlugin.cpp
        UartPlugin.h
        UartChannel.cpp
        UartChannel.h
    LINK Qt6::SerialPort
)
