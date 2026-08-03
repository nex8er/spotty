/**
 * \file PluginManager.h
 * \brief Поиск и загрузка плагинов интерфейсов.
 */
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace spotty {

class IInterfacePlugin;

/**
 * \class PluginManager
 * \brief Находит, проверяет и хранит плагины интерфейсов.
 *
 * \par Гибридная загрузка
 *
 * Закрывает обе половины гибридной сборки:
 * - Плагины, вкомпилированные в исполняемый файл, регистрируются механизмом статических
 *   плагинов Qt и забираются из `QPluginLoader::staticInstances()`.
 * - В обычной сборке дополнительно обходятся каталоги из spotty::Paths::pluginDirs() и
 *   загружаются модули (`.so`, `.dll`, `.dylib`).
 *
 * Один и тот же исходный код плагина работает в обоих режимах; выбор делает CMake-опция
 * `SPOTTY_STATIC_PLUGINS`.
 *
 * \par Плагины не выгружаются
 *
 * Загруженный плагин остаётся в памяти до конца работы программы. Канал, созданный
 * плагином, может пережить момент, когда владелец перестал на него ссылаться, а выгрузка
 * кода из-под живого QObject — гарантированное падение при первом же обращении к
 * виртуальному методу или при доставке отложенного сигнала.
 *
 * \par Проверки при загрузке
 *
 * Отклоняется всё, что:
 * - не приводится к spotty::IInterfacePlugin (посторонняя библиотека в каталоге);
 * - собрано против другой версии API (см. #SPOTTY_API_VERSION);
 * - сообщает пустой идентификатор;
 * - повторяет идентификатор уже загруженного плагина.
 *
 * Причина отказа не только пишется в журнал, но и сохраняется в failures(): молча
 * пропавший плагин — крайне неприятная в разборе неисправность.
 */
class PluginManager : public QObject
{
    Q_OBJECT

public:
    /**
     * \struct LoadFailure
     * \brief Файл, похожий на плагин, который не удалось использовать.
     */
    struct LoadFailure
    {
        QString path;   ///< Путь к файлу либо `"<static>"` для статического плагина.
        QString reason; ///< Причина отказа, пригодная для показа пользователю.
    };

    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    /**
     * \brief Найти и загрузить плагины.
     *
     * Повторные вызовы игнорируются.
     */
    void load();

    /// \return Успешно загруженные плагины. Владение остаётся за менеджером.
    const QList<IInterfacePlugin *> &plugins() const { return m_plugins; }

    /**
     * \brief Найти плагин по идентификатору.
     * \param pluginId Значение IInterfacePlugin::pluginId().
     * \return Плагин или `nullptr`.
     */
    IInterfacePlugin *plugin(const QString &pluginId) const;

    /**
     * \brief Зарегистрировать уже созданный экземпляр плагина.
     * \param instance QObject, реализующий spotty::IInterfacePlugin.
     * \param origin Откуда он взялся — попадает в сообщение об отказе.
     * \return `false`, если экземпляр отклонён; причина добавляется в failures().
     *
     * Тот же путь, которым регистрируются вкомпилированные плагины: проверки версии API,
     * пустого и повторяющегося идентификатора выполняются полностью. Публичным сделан
     * потому, что иначе ни эти проверки, ни зависящий от них реестр невозможно проверить,
     * не раскладывая собранные плагины по каталогам.
     *
     * \note Владение остаётся за вызывающей стороной.
     */
    bool addPlugin(QObject *instance, const QString &origin = QStringLiteral("<embedded>"));

    /// \return Отклонённые файлы с причинами. Показывается в стартовом отчёте.
    const QList<LoadFailure> &failures() const { return m_failures; }

    /// \return Каталоги, которые реально обходились при поиске.
    QStringList searchedDirectories() const { return m_searchedDirs; }

private:
    /// \brief Забрать плагины, вкомпилированные в исполняемый файл.
    void loadStaticPlugins();

    /// \brief Обойти каталоги и загрузить модули.
    void loadDynamicPlugins();

    /**
     * \brief Проверить и зарегистрировать экземпляр плагина.
     * \param instance Объект, полученный от QPluginLoader.
     * \param origin Путь к файлу или `"<static>"` — попадает в сообщение об отказе.
     * \return `false`, если экземпляр отклонён; причина добавлена в m_failures.
     */
    bool registerInstance(QObject *instance, const QString &origin);

    QList<IInterfacePlugin *> m_plugins; ///< Загруженные плагины.
    QList<LoadFailure> m_failures;       ///< Отклонённые файлы.
    QStringList m_searchedDirs;          ///< Обойденные каталоги.
    bool m_loaded = false;               ///< Защита от повторного вызова load().
};

} // namespace spotty
