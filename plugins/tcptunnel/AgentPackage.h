/**
 * \file AgentPackage.h
 * \brief Комплект агента: бинарники, скрипты, шаблоны.
 */
#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

namespace spotty::tcptunnel {

/**
 * \class AgentPackage
 * \brief Бинарники агента под все архитектуры, скрипты установки, шаблоны конфига и сервиса.
 *
 * Читает из двух каталогов: `binariesDir/<арх>/spotty-uartd` с `VERSION` и `filesDir/<имя>`.
 * Каталог может быть и ресурсом (`:/agent/bin`) — QFile их не различает, поэтому рабочая сборка
 * (resourceAgentPackage()) и тесты, читающие каталоги исходников, — один и тот же класс с разными
 * корнями. binary() виртуальна ради тестов: Linux-бинарник на их машине не запустить.
 */
class AgentPackage
{
public:
    AgentPackage(QString binariesDir, QString filesDir);
    virtual ~AgentPackage() = default;

    /// \brief Бинарник агента для архитектуры (`x86_64`, `armv7`…); пусто, если такого нет.
    virtual QByteArray binary(const QString &arch) const;

    /// \brief Версия комплекта (то, что напечатает `spotty-uartd --version`).
    QString version() const;

    /// \brief Текстовый файл комплекта: `detect.sh`, `install.sh`, `uninstall.sh`,
    ///        `spotty-uartd@.service`, `uartd.conf.template`. Пусто, если файла нет.
    QByteArray file(const QString &name) const;

private:
    QString m_binariesDir;
    QString m_filesDir;
};

/// \brief Прочитать файл целиком; пустой массив, если файла нет.
QByteArray readAgentFile(const QString &path);

/// \brief Комплект из ресурсов плагина (путь `:/agent/...`); так работает рабочая сборка.
std::shared_ptr<AgentPackage> resourceAgentPackage();

} // namespace spotty::tcptunnel
