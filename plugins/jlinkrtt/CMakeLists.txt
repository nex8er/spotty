# Плагин RTT поверх SEGGER J-Link. libjlinkarm — сторонняя, отдельно устанавливаемая
# библиотека; она грузится в рантайме через QLibrary (см. JlinkArmLibrary), а не линкуется
# на этапе сборки, поэтому LINK не указан и SDK от SEGGER для сборки не требуется. Плагин
# собирается всегда: без установленного J-Link software enumerate() просто вернёт пустой
# список, как UART без Qt6::SerialPort.
spotty_add_plugin(spotty-plugin-jlinkrtt
    CLASS_NAME JlinkRttPlugin
    SOURCES
        JlinkRttPlugin.cpp
        JlinkRttPlugin.h
        JlinkRttChannel.cpp
        JlinkRttChannel.h
        JlinkArmLibrary.cpp
        JlinkArmLibrary.h
        JlinkDeviceDatabase.cpp
        JlinkDeviceDatabase.h
)
