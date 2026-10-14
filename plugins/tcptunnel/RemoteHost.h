/**
 * \file RemoteHost.h
 * \brief Сведения об удалённой машине и параметры установки агента: разбор, проверка, конфиг.
 *
 * Всё здесь — чистые функции над строками: ни сети, ни ssh. Так выбор архитектуры, проверка
 * параметров и текст конфига проверяются тестами без единого соединения.
 */
#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>

namespace spotty::tcptunnel {

/// \brief Что удалось узнать об удалённой машине скриптом agent/detect.sh.
struct HostInfo
{
    /// \brief Агент, уже установленный на машине под этим портом.
    struct Instance
    {
        int port = 0;
        QString device; ///< Что записано в `device =` его конфига.
    };

    QString system;        ///< `uname -s`.
    QString machine;       ///< `uname -m`.
    QString dpkgArch;      ///< `dpkg --print-architecture`; пусто вне семейства Debian.
    int longBit = 0;       ///< Разрядность пользовательского окружения.
    int uid = -1;          ///< Кем мы вошли; 0 — root.
    QString hostname;      ///< Имя, под которым машина знает себя.
    QByteArray elf;        ///< Первые байты `/bin/sh`.
    QString osName;        ///< `PRETTY_NAME=`.
    bool systemd = false;  ///< Система загружена systemd: можно ставить сервис.
    bool hasSudo = false;
    QString installedVersion; ///< Версия агента на машине; пусто, если не установлен.
    QList<Instance> instances;
    QSet<int> listening;   ///< Порты TCP, на которых кто-то слушает.

    /// \brief Каталог бинарника в комплекте (`x86_64`, `armv7`…); пусто, если подходящего нет.
    QString agentArch;

    /// \brief Почему агент под эту машину не выбран. Пусто, если agentArch задан.
    QString archProblem;

    /// \brief Экземпляр агента на порту или `nullptr`.
    const Instance *instance(int port) const;

    /// \brief Одна строка о машине для показа пользователю.
    QString summary() const;
};

/**
 * \brief Разобрать вывод agent/detect.sh и подобрать агент под машину.
 *
 * Строки без `=` и неизвестные ключи пропускаются: скрипт может расшириться, и старый Spotty
 * не должен спотыкаться о новые поля. Результат всегда проходит chooseAgentArch().
 */
HostInfo parseHostInfo(const QString &detectOutput);

/// \brief Выбрать каталог бинарника по уже разобранным сведениям; заполняет agentArch или archProblem.
void chooseAgentArch(HostInfo *info);

/// \brief Первый порт не меньше `first`, который не занят ни агентом, ни кем-то ещё.
int nextFreePort(const HostInfo &info, int first);

/// \brief Последовательный порт удалённой машины из вывода `spotty-uartd --scan`.
struct UartPort
{
    QString device;      ///< `/dev/ttyUSB0`.
    QString stablePath;  ///< `/dev/serial/by-id/...`; пусто, если нет.
    QString description; ///< Изготовитель и изделие.
};

/// \brief Разобрать вывод `--scan` (три колонки через табуляцию, пустые поля сохраняются).
QList<UartPort> parseUartScan(const QString &output);

/// \brief Параметры, из которых строится конфиг агента.
struct InstallParams
{
    QString device;
    int baud = 115200;
    QString format = QStringLiteral("8N1");
    QString flow = QStringLiteral("none");
    int port = 7777;
    QString bind = QStringLiteral("0.0.0.0");
    QString allow; ///< Список через запятую; пусто — без ограничений.
    bool autostart = true;
};

/// \brief Скорости, которые понимает агент (agent/spotty-uartd.c, baudConstant()).
const QList<int> &supportedBauds();

/**
 * \brief Проверить параметры установки.
 * \return Пустую строку, если всё допустимо, иначе причину для пользователя.
 *
 * Проверка строгая намеренно: значения попадают в конфиг, который читает root на чужой
 * машине, и перевод строки в пути порта дописал бы туда лишнюю настройку.
 */
QString validateInstallParams(const InstallParams &params);

/// \brief Подставить параметры в шаблон конфига. Параметры должны были пройти validateInstallParams().
QString renderConfig(const QString &templateText, const InstallParams &params);

/// \brief Взять строку в одинарные кавычки для sh: `it's` → `'it'\''s'`.
QString shellQuote(const QString &text);

} // namespace spotty::tcptunnel
