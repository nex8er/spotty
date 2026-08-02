/**
 * \file main.cpp
 * \brief Точка входа: сборка служб и запуск главного окна.
 */
#include "ui/AppContext.h"
#include "ui/MainWindow.h"
#include "ui/theme/MdiIcons.h"
#include "ui/theme/ThemeManager.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <settings/Paths.h>
#include <settings/SettingsStore.h>
#include <spotty/api/ChannelState.h>

#include <QApplication>

/**
 * \brief Точка входа приложения.
 *
 * \par Порядок инициализации
 *
 * Он не произволен, каждый шаг зависит от предыдущего:
 *
 * 1. Имена организации и приложения — из них выводится каталог настроек.
 * 2. Регистрация метатипов — до первого межпоточного соединения.
 * 3. spotty::Paths — определяет переносной режим и каталог конфигурации.
 * 4. Настройки — читаются с диска, из них берётся тема.
 * 5. Оформление — до создания виджетов, иначе окно моргнёт стилем по умолчанию.
 * 6. Шрифт значков — до построения окна, иначе кнопки останутся пустыми.
 * 7. Плагины — до реестра, который их опрашивает.
 * 8. Реестр — до окна, которое показывает список устройств.
 *
 * \par Владение
 *
 * Все службы созданы здесь, на стеке, и живут до выхода из main(). Окно получает на них
 * ссылки через spotty::AppContext и ничем из этого не владеет.
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Строго до Paths::initialize(): из этих имён выводится AppConfigLocation.
    QCoreApplication::setOrganizationName(QStringLiteral("Spotty"));
    QCoreApplication::setApplicationName(QStringLiteral("Spotty"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SPOTTY_VERSION));

    // Каналы живут в потоке ввода-вывода, поэтому аргументы их сигналов пересекают границу
    // потоков и должны быть зарегистрированы до первого очередного соединения.
    qRegisterMetaType<spotty::ChannelState>();

    spotty::Paths::initialize();

    spotty::SettingsStore settings(spotty::Paths::settingsFile());
    settings.load();

    // Оформление применяется до создания виджетов: иначе окно на мгновение показалось бы
    // в стиле по умолчанию.
    spotty::ThemeManager theme;
    theme.setTheme(spotty::ThemeManager::themeFromString(
        settings.value(QStringLiteral("appearance/theme")).toString()));

    spotty::MdiIcons::initialize();

    spotty::PluginManager plugins;
    plugins.load();

    // Отдельное хранилище: устройств бывает много, и мешать их с настройками приложения
    // значило бы разрастить settings.json списком всех когда-либо виденных портов.
    spotty::SettingsStore interfaceStore(spotty::Paths::interfacesFile());
    interfaceStore.load();

    spotty::InterfaceRegistry registry(&plugins, &interfaceStore);
    registry.start();

    const spotty::AppContext context{&settings, &plugins, &registry, &theme};

    spotty::MainWindow window(context);
    window.show();

    return app.exec();
}
