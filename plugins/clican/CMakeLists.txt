# Плагин CLI поверх шины CAN через адаптер PCAN.
#
# Драйвер PEAK (PCANBasic.dll / libpcanbasic.so) и его реализация для macOS от UV Software
# (libPCBUSB.dylib) грузятся в рантайме через QLibrary — см. PcanLibrary, — а не линкуются
# на этапе сборки, поэтому LINK не указан и SDK от PEAK для сборки не нужен. Плагин
# собирается всегда: без установленного драйвера enumerate() просто вернёт пустой список,
# как UART без Qt6::SerialPort и как jlinkrtt без J-Link software.
spotty_add_plugin(spotty-plugin-clican
    CLASS_NAME CliCanPlugin
    SOURCES
        CliCanPlugin.cpp
        CliCanPlugin.h
        CliCanChannel.cpp
        CliCanChannel.h
        CliCanProtocol.h
        CliCanSettings.h
        CanBus.cpp
        CanBus.h
        PcanLibrary.cpp
        PcanLibrary.h
)
