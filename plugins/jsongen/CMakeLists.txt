# Плагин виртуального источника JSON. Ничего сверх SDK ему не нужно (QJsonDocument лежит в
# Qt6::Core), поэтому LINK не указан.
spotty_add_plugin(spotty-plugin-jsongen
    CLASS_NAME JsonGenPlugin
    SOURCES
        JsonGenPlugin.cpp
        JsonGenPlugin.h
        JsonGenChannel.cpp
        JsonGenChannel.h
)
