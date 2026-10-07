/**
 * \file Com0com.h
 * \brief Работа с драйвером com0com: обнаружение, создание и удаление пар (только Windows).
 *
 * На Windows виртуальный порт без драйвера не создать, и перехватчику нужна пара com0com.
 * Пару Spotty заводит сам: один конец получает имя, выбранное пользователем, — его и
 * открывает чужая программа, — второй прячется от перечисления портов (`HiddenMode`) и
 * достаётся самому Spotty. В списках портов чужой программы второй конец поэтому не мешает.
 */
#pragma once

#include <QList>
#include <QString>

namespace spotty::uartproxy {

/// \brief Один конец пары com0com.
struct Com0comPort
{
    QString device; ///< Собственное имя устройства драйвера: `CNCA0`, `CNCB0`.
    QString name;   ///< Имя, под которым порт открывают программы (`COM20` или `CNCA0`).
    bool hidden = false; ///< Скрыт от перечисления портов (`HiddenMode=yes`).
};

/// \brief Пара связанных портов: что пишут в один конец, читают из другого.
struct Com0comPair
{
    int number = -1; ///< Номер пары у драйвера — им пару удаляют.
    Com0comPort a;
    Com0comPort b;
};

/// \brief Что известно о com0com на этой машине.
struct Com0comInfo
{
    /// \brief Драйвер зарегистрирован в системе (служба `com0com`).
    bool driverInstalled = false;

    /// \brief Каталог установки, где лежат `setupc.exe` и `setupg.exe`; пусто, если не найден.
    QString installDir;

    /// \brief Пары, присутствующие сейчас.
    QList<Com0comPair> pairs;

    /// \return Путь к графической программе настройки или пустую строку.
    QString setupGui() const;

    /// \return Путь к консольной программе настройки или пустую строку.
    QString setupCli() const;
};

/**
 * \brief Опросить реестр и файловую систему.
 *
 * Только чтение реестра и проверка существования устройств — дёшево, годится для вызова раз
 * в секунду из spotty::IInterfacePlugin::liveOptions().
 */
Com0comInfo detectCom0com();

/**
 * \brief Пара, один из концов которой открывается под именем \p name.
 * \param ownEnd Куда записать найденный конец; может быть `nullptr`.
 * \param peerEnd Куда записать второй конец; может быть `nullptr`.
 */
const Com0comPair *findCom0comPair(const Com0comInfo &info, const QString &name,
                                   Com0comPort *ownEnd = nullptr,
                                   Com0comPort *peerEnd = nullptr);

/// \return Существует ли в системе устройство с таким именем (любое, не только com0com).
bool portNameTaken(const QString &name);

/// \return Пригодно ли имя для порта: непустое, без пробелов и символов `,=\/`.
bool isValidPortName(const QString &name);

/**
 * \brief Вернуть имя скрытого конца пары для \p visibleName, создав пару при нужде.
 * \param error Куда записать причину отказа.
 * \param createdPair Куда записать номер пары, если её создал этот вызов; не трогается,
 *        если пара уже существовала. Нужен, чтобы при выходе удалить только своё.
 * \return Путь устройства (`\\.\CNCB0`), который открывает Spotty, или пустую строку.
 *
 * Создание требует прав администратора (запрос UAC) и ждёт, пока драйвер поставит
 * устройства, — до минуты. Поэтому вызывать только из потока ввода-вывода, не из UI.
 */
QString ensureCom0comPair(const QString &visibleName, QString *error,
                          int *createdPair = nullptr);

/**
 * \brief Удалить пару, не дожидаясь завершения.
 * \return `true`, если удаление запущено или пользователь сам отказал в правах.
 *
 * Ждёт только ответа на запрос UAC: вызывается из потока UI, а результат увидит опрос
 * строки состояния.
 */
bool removeCom0comPair(const Com0comInfo &info, int number, QString *error);

/**
 * \brief Запустить графическую программу настройки com0com.
 * \return `true`, если программа запущена или пользователь сам отказал в правах.
 *
 * Программа требует прав администратора, а `QProcess` запрос повышения не поднимает
 * (CreateProcess отвечает `ERROR_ELEVATION_REQUIRED`), поэтому запуск идёт через оболочку.
 */
bool launchCom0comSetup(const Com0comInfo &info, QString *error);

} // namespace spotty::uartproxy
