/**
 * \file main.cpp
 * \brief Точка входа: сборка служб и запуск главного окна.
 */
#include "ui/AppContext.h"
#include "ui/MainWindow.h"
#include "ui/PanelPluginRegistry.h"
#include "ui/theme/EmbeddedFonts.h"
#include "ui/theme/MdiIcons.h"
#include "ui/theme/ThemeManager.h"

#include <HistoryStore.h>
#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <Session.h>
#include <SingleInstanceGuard.h>
#include <settings/AppSettings.h>
#include <settings/Paths.h>
#include <settings/SettingsMigration.h>
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
 * 8. Шрифты — до построения окна: без шрифта значков кнопки останутся пустыми, а
 *    терминал успеет посчитать метрики системным шрифтом вместо встроенного.
 * 9. Плагины — до реестров, которые разбирают их роли.
 * 10. Реестр панелей — вторая фаза загрузки; после неё PluginManager::finishLoading()
 *     заносит в отчёт всё, чью роль не признал никто.
 * 11. Реестр интерфейсов — до сессии, которая берёт из него настройки устройства.
 * 12. Сессия — до окна, которое показывает её буфер.
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

    // До первого чтения настроек плагинами: они начнут читать своё пространство имён из
    // конструкторов панелей, то есть уже при построении окна.
    spotty::SettingsMigration::apply(settings);

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
    spotty::EmbeddedFonts::initialize();

    spotty::PluginManager plugins;
    // Список выключенных отдаётся обоим реестрам ролей, и обоим по отдельности: роль
    // плагина к этому моменту ещё не разобрана, а один объект вправе играть обе.
    plugins.setDisabledPlugins(appSettings.disabledPlugins);
    plugins.load();

    // Вторая фаза: панельную роль разбирает реестр из слоя UI — панельный SDK линкуется с
    // Qt6::Widgets, и ядру он недоступен. Макросы, журнал, поиск и генератор приходят тем
    // же путём, что и сторонние: они обычные плагины и ничем не привилегированы.
    spotty::PanelPluginRegistry panels(&plugins);
    panels.setDisabledPlugins(appSettings.disabledPlugins);
    panels.load();

    // Только теперь отчёт о загрузке полон: до этого момента плагин чужой роли выглядел
    // бы нераспознанным.
    plugins.finishLoading();

    // Отдельное хранилище: устройств бывает много, и мешать их с настройками приложения
    // значило бы разрастить settings.json списком всех когда-либо виденных портов.
    spotty::SettingsStore interfaceStore(spotty::Paths::interfacesFile());
    interfaceStore.load();

    spotty::InterfaceRegistry registry(&plugins, &interfaceStore);
    registry.start();

    spotty::HistoryStore history(spotty::Paths::historyFile(), appSettings.historySize);
    history.load();

    spotty::Session session(&plugins, &registry);

    const spotty::AppContext context{&settings, &plugins, &panels,  &registry,
                                     &theme,    &session, &history};

    spotty::MainWindow window(context);
    QObject::connect(&guard, &spotty::SingleInstanceGuard::raiseRequested,
                     &window, &spotty::MainWindow::raiseWindow);
    window.show();

    return app.exec();
}
