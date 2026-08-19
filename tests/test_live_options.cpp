/**
 * \file test_live_options.cpp
 * \brief Тесты живых списков в диалоге настроек интерфейса.
 *
 * Проверяется именно проводка: что окно строится сразу, не дожидаясь опроса; что пункты
 * доезжают до списка по мере появления; что выбранное пользователем переживает
 * обновление; и что закрытое окно перестаёт спрашивать — на этом держится освобождение
 * шины CAN, которую плагин занимает только ради поиска узлов.
 */
#include "support/FakeInterfacePlugin.h"
#include "support/TestSupport.h"

#include "dialogs/InterfaceSettingsPanel.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <settings/SettingsStore.h>

#include <QComboBox>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::FakeInterfacePlugin;
using spotty::test::TempDir;
using spotty::test::waitFor;

namespace {

constexpr auto kLiveKey = "node";

/**
 * \class LivePlugin
 * \brief Плагин с одним полем, пункты которого приходят опросом.
 *
 * Изображает spotty::CliCanPlugin, не требуя ни шины, ни драйвера: «найденное» задаётся
 * присваиванием, а счётчик вызовов показывает, продолжает ли ядро спрашивать.
 */
class LivePlugin : public FakeInterfacePlugin
{
public:
    SettingsSchema settingsSchema() const override
    {
        SettingsSchema schema;
        schema.add(SettingsField{
            .key = QLatin1String(kLiveKey),
            .label = QStringLiteral("Node"),
            .group = QStringLiteral("Bus"),
            .type = SettingsField::Choice,
            .defaultValue = 0,
            .options = {},
            .live = true,
            .editable = true,
        });
        return schema;
    }

    QList<SettingsOption> liveOptions(const InterfaceDescriptor &descriptor, const QString &key,
                                      const QVariantMap &settings) override
    {
        Q_UNUSED(descriptor);
        Q_UNUSED(settings);
        if (key != QLatin1String(kLiveKey))
            return {};

        ++calls;
        QList<SettingsOption> options;
        for (int node : found)
            options.append(SettingsOption{QString::number(node), node});
        return options;
    }

    /// \brief Узлы, «отозвавшиеся» к этому моменту. Меняется прямо из теста.
    QList<int> found;

    /// \brief Сколько раз ядро спросило про пункты.
    int calls = 0;
};

/**
 * \struct Fixture
 * \brief Реестр с одним устройством живого плагина и панель настроек над ним.
 */
struct Fixture
{
    TempDir dir;
    PluginManager plugins;
    LivePlugin plugin;
    SettingsStore store;
    InterfaceRegistry registry;

    Fixture()
        : store(dir.filePath(QStringLiteral("interfaces.json")))
        , registry(&plugins, &store)
    {
        EXPECT_TRUE(plugins.addPlugin(&plugin));
        store.load();
        plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                          QStringLiteral("dev-a"))};
        registry.refresh();
    }

    /// \brief Редактор живого поля панели.
    static QComboBox *liveEditor(InterfaceSettingsPanel *panel)
    {
        // По имени, а не по порядку: первым списком в панели идёт переключатель устройств,
        // и «первый попавшийся QComboBox» — это он.
        return panel->findChild<QComboBox *>(QStringLiteral("schemaField_") + QLatin1String(kLiveKey));
    }
};

} // namespace

TEST(LiveOptions, ShowsTheFormBeforeAnythingIsFound)
{
    Fixture fixture;
    InterfaceSettingsPanel panel(&fixture.registry, &fixture.plugins);
    panel.selectInterface(QStringLiteral("fake:a"));

    // Пустой опрос — не повод не показывать поле: окно должно быть готово к работе
    // сразу, а список наполняться потом.
    QComboBox *editor = Fixture::liveEditor(&panel);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->count(), 0);
}

TEST(LiveOptions, FillsTheListAsNodesAnswer)
{
    Fixture fixture;
    InterfaceSettingsPanel panel(&fixture.registry, &fixture.plugins);
    panel.show();
    panel.selectInterface(QStringLiteral("fake:a"));

    QComboBox *editor = Fixture::liveEditor(&panel);
    ASSERT_NE(editor, nullptr);

    fixture.plugin.found = {5};
    ASSERT_TRUE(waitFor([editor] { return editor->count() == 1; }, 3000));
    EXPECT_EQ(editor->itemText(0), QStringLiteral("5"));
    EXPECT_EQ(editor->itemData(0).toInt(), 5);

    // Второй узел отозвался позже — список дополняется, а не пересобирается с нуля.
    fixture.plugin.found = {5, 12};
    ASSERT_TRUE(waitFor([editor] { return editor->count() == 2; }, 3000));
    EXPECT_EQ(editor->itemText(1), QStringLiteral("12"));
}

TEST(LiveOptions, KeepsWhatTheUserPickedWhileTheListGrows)
{
    Fixture fixture;
    fixture.plugin.found = {5};

    InterfaceSettingsPanel panel(&fixture.registry, &fixture.plugins);
    panel.show();
    panel.selectInterface(QStringLiteral("fake:a"));

    QComboBox *editor = Fixture::liveEditor(&panel);
    ASSERT_NE(editor, nullptr);
    ASSERT_TRUE(waitFor([editor] { return editor->count() == 1; }, 3000));

    editor->setCurrentIndex(0);
    EXPECT_EQ(fixture.registry.settingsFor(QStringLiteral("fake:a"))
                  .value(QLatin1String(kLiveKey))
                  .toInt(),
              5);

    // Приход нового узла не должен сбрасывать выбор: список обновляется под рукой
    // пользователя, и потерянный выбор выглядел бы как самопроизвольная смена платы.
    fixture.plugin.found = {2, 5};
    ASSERT_TRUE(waitFor([editor] { return editor->count() == 2; }, 3000));
    EXPECT_EQ(editor->currentData().toInt(), 5);
    EXPECT_EQ(fixture.registry.settingsFor(QStringLiteral("fake:a"))
                  .value(QLatin1String(kLiveKey))
                  .toInt(),
              5);
}

TEST(LiveOptions, KeepsANodeNumberTypedByHand)
{
    Fixture fixture;
    InterfaceSettingsPanel panel(&fixture.registry, &fixture.plugins);
    panel.show();
    panel.selectInterface(QStringLiteral("fake:a"));

    QComboBox *editor = Fixture::liveEditor(&panel);
    ASSERT_NE(editor, nullptr);

    // Узел, который сейчас молчит, набирают номером — и он обязан пережить приход чужих
    // пунктов, иначе набранное исчезало бы прямо во время набора.
    editor->setCurrentText(QStringLiteral("77"));

    fixture.plugin.found = {2};
    ASSERT_TRUE(waitFor([editor] { return editor->count() == 1; }, 3000));
    EXPECT_EQ(editor->currentText(), QStringLiteral("77"));
    EXPECT_EQ(fixture.registry.settingsFor(QStringLiteral("fake:a"))
                  .value(QLatin1String(kLiveKey))
                  .toInt(),
              77);
}

TEST(LiveOptions, StopsAskingWhenTheDialogIsClosed)
{
    Fixture fixture;
    InterfaceSettingsPanel panel(&fixture.registry, &fixture.plugins);
    panel.show();
    panel.selectInterface(QStringLiteral("fake:a"));

    ASSERT_TRUE(waitFor([&fixture] { return fixture.plugin.calls > 0; }, 3000));

    panel.hide();
    const int afterHide = fixture.plugin.calls;

    // Прекратившиеся вызовы — единственный способ сообщить плагину, что опрос больше не
    // нужен: именно по нему spotty::CliCanPlugin отпускает шину CAN.
    EXPECT_FALSE(waitFor([&fixture, afterHide] { return fixture.plugin.calls > afterHide; },
                         1500));
}
