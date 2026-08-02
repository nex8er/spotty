# Плагин виртуальных каналов. Ничего сверх SDK ему не нужно, поэтому LINK не указан.
spotty_add_plugin(spotty-plugin-loopback
    CLASS_NAME LoopbackPlugin
    SOURCES
        LoopbackPlugin.cpp
        LoopbackPlugin.h
        LoopbackChannel.cpp
        LoopbackChannel.h
)
