/**
 * \file main.cpp
 * \brief Точка входа: сборка служб и запуск главного окна.
 */
#include "ui/AppContext.h"
#include "ui/MainWindow.h"
#include "ui/theme/MdiIcons.h"
#include "ui/theme/ThemeManager.h"

#include <HistoryStore.h>
#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <Session.h>
#include <SingleInstanceGuard.h>
#include <settings/AppSettings.h>
#include <settings/Paths.h>
#include <settings/SettingsStore.h>
#include <spotty/api/ChannelState.h>

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace {

/**
 * \brief Загрузить перевод интерфейса и перевод стандартных строк Qt.
 * \param app Приложение, которому устанавливаются переводчики.
 * \param language `"system"`, `"en"` или `"ru"`.
 *
 * Переводчики живут до конца работы программы, поэтому создаются на куче с приложением
 * в родителях. Снимать их не нужно: смена языка требует перезапуска.
 *
 * Отсутствие файла перевода не ошибка — интерфейс останется на английском, который и
 * является исходным языком строк.
 */
void installTranslations(QApplication &app, const QString &language)
{
    const QLocale locale = language == QLatin1String("system")
                               ? QLocale::system()
                               : QLocale(language);

    // Перевод стандартных строк Qt: кнопки «ОК» и «Отмена», названия месяцев, пункты
    // контекстного меню полей ввода. Без него диалоги оказались бы наполовину переведены.
    auto *qtTranslator = new QTranslator(&app);
    if (qtTranslator->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(qtTranslator);
    }

    auto *translator = new QTranslator(&app);
    if (translator->load(locale, QStringLiteral("spotty"), QStringLiteral("_"),
                         QStringLiteral(":/i18n"))) {
        app.installTranslator(translator);
    }
}

} // namespace

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
 * 4. Настройки — читаются с диска; из них берутся язык, тема и всё остальное.
 * 5. Проверка единственного экземпляра — до создания окна, чтобы второй экземпляр
 *    завершился, не построив интерфейс впустую.
 * 6. Переводы — до создания виджетов: строки берутся при их построении.
 * 7. Оформление — тоже до виджетов, иначе окно моргнёт стилем по умолчанию.
 * 8. Шрифт значков — до построения окна, иначе кнопки останутся пустыми.
 * 9. Плагины — до реестра, который их опрашивает.
 * 10. Реестр — до сессии, которая берёт из него настройки устройства.
 * 11. Сессия — до окна, которое показывает её буфер.
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

    const spotty::AppSettings appSettings = spotty::AppSettings::load(settings);

    // Второй экземпляр завершается до построения интерфейса: строить окно, чтобы тут же
    // его выбросить, значит зря мигнуть им на экране.
    spotty::SingleInstanceGuard guard(QStringLiteral("spotty"));
    if (appSettings.singleInstance && !guard.tryAcquire()) {
        guard.notifyExisting();
        return 0;
    }

    installTranslations(app, appSettings.language);

    // Оформление применяется до создания виджетов: иначе окно на мгновение показалось бы
    // в стиле по умолчанию.
    spotty::ThemeManager theme;
    theme.setTheme(spotty::ThemeManager::themeFromString(appSettings.theme));

    spotty::MdiIcons::initialize();

    spotty::PluginManager plugins;
    plugins.load();

    // Отдельное хранилище: устройств бывает много, и мешать их с настройками приложения
    // значило бы разрастить settings.json списком всех когда-либо виденных портов.
    spotty::SettingsStore interfaceStore(spotty::Paths::interfacesFile());
    interfaceStore.load();

    spotty::InterfaceRegistry registry(&plugins, &interfaceStore);
    registry.start();

    spotty::HistoryStore history(spotty::Paths::historyFile(), appSettings.historySize);
    history.load();

    spotty::Session session(&plugins, &registry);

    const spotty::AppContext context{&settings, &plugins,  &registry,
                                     &theme,    &session,  &history};

    spotty::MainWindow window(context);
    QObject::connect(&guard, &spotty::SingleInstanceGuard::raiseRequested,
                     &window, &spotty::MainWindow::raiseWindow);
    window.show();

    return app.exec();
}
