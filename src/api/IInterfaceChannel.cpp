/**
 * \file IInterfaceChannel.cpp
 * \brief Внеклассовые определения spotty::IInterfaceChannel.
 *
 * Единственная единица трансляции библиотеки SDK. Существует ради того, чтобы у
 * класса с Q_OBJECT было ровно одно определение метаобъекта, таблицы виртуальных функций
 * и typeinfo, общее для приложения и всех плагинов.
 *
 * \see SpottyApiExport.h — подробное объяснение, почему это критично.
 */
#include <spotty/api/IInterfaceChannel.h>

namespace spotty {

IInterfaceChannel::IInterfaceChannel(QObject *parent)
    : QObject(parent)
{
}

IInterfaceChannel::~IInterfaceChannel() = default;

} // namespace spotty
