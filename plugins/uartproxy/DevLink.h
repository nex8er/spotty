/**
 * \file DevLink.h
 * \brief Постоянное имя виртуального порта в `/dev` (POSIX).
 */
#pragma once

#include <QString>

#include <functional>

namespace spotty::uartproxy {

/**
 * \brief Промежуточная ссылка на подчинённый конец pty, которой владеет Spotty.
 *
 * Ссылка в `/dev` ведёт не прямо на pty, а сюда. Создать её в `/dev` может только
 * администратор, а номер pty меняется при каждом запуске: будь ссылка прямой, права
 * спрашивались бы на каждом открытии. Так спрашиваются один раз — пока система не
 * перезагружена и `/dev` не очистился, — а дальше Spotty лишь переставляет эту ссылку в
 * своём каталоге.
 */
QString bridgePath();

/// \brief Подменить промежуточную ссылку; только для тестов, иначе они трогали бы настоящую.
void setBridgePathForTesting(const QString &path);

/**
 * \brief Можно ли вообще создавать имена в `/dev`.
 *
 * На macOS нельзя: `/dev` — это devfs, и `ln -s` там отвечает `Operation not permitted`
 * даже после ввода пароля администратора (проверено на живой системе). Спрашивать пароль
 * ради заведомого отказа бессмысленно, поэтому имена в `/dev` на macOS не создаются вовсе.
 */
bool deviceLinksSupported();

/**
 * \brief Нужно ли имя класть в `/dev`.
 *
 * Только там порты и ищут чужие программы: списки строятся по `/dev/cu.*` и `/dev/tty.*`.
 */
bool isDevicePath(const QString &path);

/**
 * \brief Годится ли имя для создания с правами администратора.
 *
 * Путь после проверки подставляется в команду, которая исполняется от имени root, поэтому
 * допустимо ровно `/dev/<имя>` из безопасных знаков: никаких подкаталогов, `..`, пробелов и
 * кавычек. Проверка стоит перед самым запуском и от настроек не зависит.
 */
bool isValidDevicePath(const QString &path);

/// \brief Что лежит по имени сейчас.
enum class LinkState {
    Missing, ///< Ничего нет: ссылку можно создать.
    Ready,   ///< Ссылка уже ведёт на промежуточную: создавать не нужно.
    Foreign, ///< Занято чужим — другой ссылкой или не ссылкой: не трогать.
};

/// \brief Состояние имени относительно ссылки на \p target.
LinkState inspectLink(const QString &path, const QString &target);

/// \brief Состояние имени в `/dev` относительно промежуточной ссылки.
LinkState inspectDeviceLink(const QString &path);

/**
 * \brief Исполнитель с повышением прав: просит у пользователя пароль и запускает \p script.
 * \param script Текст для `/bin/sh`.
 * \param cancelled Выставляется, если пользователь отказался.
 * \param error Причина неудачи, когда это не отказ.
 */
using Elevator = std::function<bool(const QString &script, bool *cancelled, QString *error)>;

/// \brief Подменить исполнителя (тесты); пустой функтор возвращает настоящего.
void setElevatorForTesting(Elevator elevator);

/**
 * \brief Создать в `/dev` ссылку на промежуточную; без дела прав не просит.
 * \return `true`, если после вызова имя ведёт на промежуточную ссылку.
 */
bool ensureDeviceLink(const QString &path, QString *error);

/// \brief Удалить ссылку в `/dev`, если она ведёт на промежуточную. Чужое не трогает.
bool removeDeviceLink(const QString &path, QString *error);

} // namespace spotty::uartproxy
