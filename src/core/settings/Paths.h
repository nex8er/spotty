/**
 * \file Paths.h
 * \brief Все пути файловой системы, которыми пользуется Spotty.
 */
#pragma once

#include <QString>
#include <QStringList>

namespace spotty {

/**
 * \class Paths
 * \brief Разрешение путей к настройкам, логам и плагинам.
 *
 * Все методы статические: пути определяются один раз при запуске и дальше не меняются.
 *
 * \par Переносной режим
 *
 * Если рядом с исполняемым файлом (а на macOS — рядом с бандлом `.app`) лежит файл
 * `spotty-portable.txt`, вся конфигурация переезжает в каталог `config` рядом с ним,
 * вместо пользовательского каталога настроек системы. Так весь комплект целиком живёт на
 * флешке и переносится между машинами.
 *
 * Содержимое файла-маркера не читается — важно само его наличие.
 *
 * \par Порядок вызовов
 *
 * \code
 * QApplication app(argc, argv);
 * QCoreApplication::setOrganizationName(QStringLiteral("Spotty"));
 * QCoreApplication::setApplicationName(QStringLiteral("Spotty"));
 * Paths::initialize();   // только после установки имён: из них выводится AppConfigLocation
 * \endcode
 */
class Paths
{
public:
    /**
     * \brief Определить режим работы и расположение конфигурации.
     *
     * \warning Вызывать один раз, после создания QCoreApplication и установки имён
     *          организации и приложения, но до любого другого метода этого класса.
     *          Повторный вызов ничего не делает.
     */
    static void initialize();

    /// \return `true`, если обнаружен маркер переносного режима.
    static bool isPortable();

    /**
     * \brief Каталог конфигурации.
     * \return В обычном режиме — `QStandardPaths::AppConfigLocation`, в переносном —
     *         `<каталог приложения>/config`.
     */
    static QString configDir();

    /// \return Путь к `settings.json` — общие настройки приложения.
    static QString settingsFile();

    /// \return Путь к `interfaces.json` — псевдонимы и настройки устройств.
    static QString interfacesFile();

    /// \return Путь к `history.txt` — история строки отправки.
    static QString historyFile();

    /// \return Каталог `macros` — по файлу на пресет макросов.
    static QString macrosDir();

    /**
     * \brief Каталог логов по умолчанию.
     * \return В переносном режиме — `<каталог приложения>/logs`, иначе
     *         `<Документы>/Spotty/logs`.
     *
     * \note Пользователь может переопределить путь в настройках; это лишь значение по
     *       умолчанию при первом запуске.
     */
    static QString defaultLogDir();

    /**
     * \brief Каталоги, в которых ищутся загружаемые плагины.
     * \return Только реально существующие каталоги, без повторов, в порядке приоритета:
     *         1. пути из переменной окружения `SPOTTY_PLUGIN_PATH`
     *            (разделитель `:`, в Windows `;`);
     *         2. `<каталог конфигурации>/plugins` — плагины, установленные пользователем;
     *         3. каталог плагинов внутри приложения
     *            (на macOS `Contents/PlugIns/spotty`, иначе `plugins` рядом с бинарником);
     *         4. `<каталог приложения>/../lib/spotty/plugins` — раскладка при установке.
     *
     * Порядок значим: spotty::PluginManager при совпадении идентификаторов оставляет
     * найденный раньше. Благодаря этому свежую сборку плагина можно положить в
     * пользовательский каталог и она перекроет штатную.
     */
    static QStringList pluginDirs();

private:
    /**
     * \brief Каталог, относительно которого ищется маркер переносного режима.
     * \return На macOS — каталог, содержащий бандл `.app`; на остальных системах —
     *         каталог исполняемого файла.
     */
    static QString applicationRootDir();
};

} // namespace spotty
