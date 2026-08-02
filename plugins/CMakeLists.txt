# Плагины интерфейсов.
#
# Порядок не важен: каждый плагин самодостаточен и объявляется одним вызовом
# spotty_add_plugin() в собственном CMakeLists.txt.
#
# Плагин uart появится на этапе 2 и будет добавлен под условием Qt6SerialPort_FOUND.
add_subdirectory(loopback)
