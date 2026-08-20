# Плагин NAB USB для Windows. libusbK ставится вместе с драйвером устройства и грузится
# в рантайме, поэтому SDK или import-библиотека для сборки не нужны.
spotty_add_plugin(spotty-plugin-nab
    CLASS_NAME NabPlugin
    SOURCES
        NabPlugin.cpp
        NabPlugin.h
        NabChannel.cpp
        NabChannel.h
        LibusbKLibrary.cpp
        LibusbKLibrary.h
)
