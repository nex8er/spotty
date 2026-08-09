# Плагин виртуального источника математических сигналов. Ничего сверх SDK ему не нужно,
# поэтому LINK не указан.
spotty_add_plugin(spotty-plugin-signalgen
    CLASS_NAME SignalGenPlugin
    SOURCES
        SignalGenPlugin.cpp
        SignalGenPlugin.h
        SignalGenChannel.cpp
        SignalGenChannel.h
)
