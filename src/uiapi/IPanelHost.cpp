/**
 * \file IPanelHost.cpp
 * \brief Ключевая функция spotty::IPanelHost.
 *
 * Здесь нет логики и не должно быть: файл существует ради того, чтобы у класса появилась
 * ключевая функция — первый невстроенный виртуальный метод. Компилятор порождает таблицу
 * виртуальных функций и typeinfo рядом с ней, то есть один раз, в этой библиотеке. Без
 * этого каждый плагин получил бы собственную копию, а Qt сравнивает метаобъекты по
 * указателю.
 */
#include <spotty/ui/IPanelHost.h>

namespace spotty {

IPanelHost::IPanelHost(QObject *parent)
    : QObject(parent)
{
}

IPanelHost::~IPanelHost() = default;

} // namespace spotty
