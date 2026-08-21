# Панель разбора JSON. Логика — фреймер и дерево — живёт в SDK (Spotty::Api), поэтому
# здесь остаётся только показ; ничего сверх Spotty::UiApi ему не нужно.
spotty_add_plugin(spotty-plugin-jsontree
    CLASS_NAME JsonTreePlugin
    KIND PANEL
    SOURCES
        JsonTreePlugin.cpp
        JsonTreePlugin.h
        JsonTreePanel.cpp
        JsonTreePanel.h
        JsonTreeView.cpp
        JsonTreeView.h
        JsonRateDelegate.cpp
        JsonRateDelegate.h
)
