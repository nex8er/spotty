/**
 * \file ResourceAgentPackage.cpp
 * \brief Комплект агента из ресурсов плагина.
 *
 * Отдельный файл от AgentPackage.cpp: он тянет за собой символы ресурсов, которых нет в
 * сборках, где комплект читается с диска (тесты).
 */
#include "AgentPackage.h"

#include <QtResource>

// Вне пространства имён: Q_INIT_RESOURCE объявляет внешнюю функцию, и внутри namespace она
// получила бы чужое имя. Нужен в статической сборке — ресурсы библиотеки сами не
// регистрируются, и без вызова :/agent/... пусто; в динамической вызов безвреден.
static void initAgentResources()
{
    Q_INIT_RESOURCE(tcptunnel_agent_bin);
    Q_INIT_RESOURCE(tcptunnel_agent_pkg);
}

namespace spotty::tcptunnel {

std::shared_ptr<AgentPackage> resourceAgentPackage()
{
    initAgentResources();
    return std::make_shared<AgentPackage>(QStringLiteral(":/agent/bin"), QStringLiteral(":/agent/pkg"));
}

} // namespace spotty::tcptunnel
