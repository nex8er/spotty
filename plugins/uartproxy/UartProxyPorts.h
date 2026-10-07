/**
 * \file UartProxyPorts.h
 * \brief Работа с последовательными портами, общая для плагина и канала.
 */
#pragma once

#include <spotty/api/SettingsSchema.h>

#include <QList>
#include <QSerialPortInfo>
#include <QString>
#include <QVariantMap>

class QSerialPort;

namespace spotty::uartproxy {

/**
 * \brief Устойчивый идентификатор порта: по свойствам железа, а не по имени узла.
 *
 * Тот же приём, что у плагина `uart`: `/dev/cu.usbserial-1420` после переподключения
 * легко становится `...-1430`, а сохранённый в настройках перехвата порт обязан остаться
 * тем же. Плагины друг с другом не линкуются, поэтому правило повторено, а не вызвано.
 */
QString stablePortId(const QSerialPortInfo &info);

/**
 * \brief Системное имя порта по значению из настроек.
 * \param value Устойчивый идентификатор из stablePortId() либо имя, введённое вручную.
 * \return Найденное имя; если такого идентификатора сейчас нет, значение возвращается как
 *         есть — его могли ввести руками (`/dev/ttys004`, `COM7`).
 */
QString resolvePortName(const QString &value);

/**
 * \brief Пункты списка портов для живого поля настроек.
 * \param byName `true` — значением пункта служит имя порта, `false` — устойчивый
 *        идентификатор.
 */
QList<SettingsOption> portOptions(bool byName);

/**
 * \brief Применить скорость и формат кадра из настроек к объекту порта.
 * \param error Причина отказа; может быть `nullptr`.
 */
bool configureSerial(QSerialPort *port, const QVariantMap &settings, QString *error);

} // namespace spotty::uartproxy
