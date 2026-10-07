# Доступ к потоку интерфейса для других программ: локальный сокет или TCP, байты как есть.
# Сервер (StreamServer) не знает о виджетах и проверяется отдельно; панель — только проводка.
spotty_add_plugin(spotty-plugin-streamserver
    CLASS_NAME StreamServerPlugin
    KIND PANEL
    SOURCES
        StreamServerPlugin.cpp
        StreamServerPlugin.h
        StreamServerPanel.cpp
        StreamServerPanel.h
        StreamServer.cpp
        StreamServer.h
    LINK Qt6::Network
)
