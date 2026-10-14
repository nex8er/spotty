/**
 * \file HostNames.h
 * \brief Имена хостов, узнанные Spotty: из них составляется подпись подключения.
 */
#pragma once

#include <QMutex>
#include "TabFileStore.h"

namespace spotty::tcptunnel {

/**
 * \class HostNames
 * \brief «Адрес → имя машины»: `10.0.0.49` → `raspberrypi`.
 *
 * Подпись подключения по умолчанию — `имя ttyUSB0`, а не `адрес:порт`: имя читается и не меняется
 * вместе с адресом из DHCP. Имя приходит двумя путями: от самой машины (`hostname`, когда
 * Spotty заходил на неё по ssh) и из обратного DNS (сканер сети). Настройки подключения плагин
 * писать не вправе, поэтому узнанное хранится отдельно, в каталоге данных плагина.
 *
 * Потокобезопасен.
 */
class HostNames : private TabFileStore
{
public:
    /// \param filePath Файл хранилища; пустой путь — только память.
    explicit HostNames(QString filePath = {});

    /// \brief Задать файл и прочитать его.
    using TabFileStore::setFile;

    /// \return Короткое имя машины или пустую строку, если не известно.
    QString nameFor(const QString &host) const;

    /**
     * \brief Запомнить имя машины.
     * \return `true`, если запись изменилась (стоит перерисовать подписи).
     *
     * Имя сокращается до первой метки (`raspberrypi.fritz.box` → `raspberrypi`). Пустое имя и
     * имя, равное самому адресу, не запоминаются: подпись «10.0.0.5:7777» от этого не
     * улучшилась бы.
     */
    bool remember(const QString &host, const QString &name);

    /// \brief Короткое имя из полного; пусто, если это адрес, а не имя.
    static QString shortName(const QString &name);
};

} // namespace spotty::tcptunnel
