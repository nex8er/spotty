/**
 * \file test_interface_settings_panel.cpp
 * \brief Тесты раскладки панели настроек интерфейса.
 */
#include "support/FakeInterfacePlugin.h"
#include "support/TestSupport.h"

#include "dialogs/InterfaceSettingsPanel.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <settings/SettingsStore.h>

#include <QComboBox>
#include <QScrollArea>
#include <QScrollBar>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::FakeInterfacePlugin;
using spotty::test::TempDir;

namespace {

constexpr int kFieldCount = 14;

/// \brief Плагин с заведомо длинной схемой: у каждого поля подсказка, которая переносится.
class LongSchemaPlugin : public FakeInterfacePlugin
{
public:
    SettingsSchema settingsSchema() const override
    {
        SettingsSchema schema;
        for (int i = 0; i < kFieldCount; ++i) {
            schema.add(SettingsField{
                .key = QStringLiteral("field%1").arg(i),
                .label = QStringLiteral("Field %1").arg(i),
                .group = QStringLiteral("Group"),
                .type = SettingsField::Choice,
                .defaultValue = 1,
                .options = {{QStringLiteral("one"), 1}, {QStringLiteral("two"), 2}},
                .hint = QStringLiteral("A hint long enough to wrap onto several lines when "
                                       "the panel is narrow, as the real ones do."),
            });
        }
        return schema;
    }
};

} // namespace

TEST(InterfaceSettingsPanel, LongSchemaScrollsInsteadOfOverlappingFields)
{
    TempDir dir;
    PluginManager plugins;
    LongSchemaPlugin plugin;
    SettingsStore store(dir.filePath(QStringLiteral("interfaces.json")));
    InterfaceRegistry registry(&plugins, &store);
    ASSERT_TRUE(plugins.addPlugin(&plugin));
    store.load();
    plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"),
                                                      QStringLiteral("dev-a"))};
    registry.refresh();

    InterfaceSettingsPanel panel(&registry, &plugins);
    panel.selectInterface(QStringLiteral("fake:a"));

    // Заведомо меньше, чем нужно полям: так выглядит диалог, в который схема плагина не
    // поместилась. Прежде Qt в этом случае ужимал поля ниже минимума, и они накладывались.
    panel.resize(420, 300);
    panel.show();

    auto *scroll = panel.findChild<QScrollArea *>();
    ASSERT_NE(scroll, nullptr);
    EXPECT_GT(scroll->verticalScrollBar()->maximum(), 0);

    // Каждое поле получило не меньше места, чем ему нужно, и поля не заходят друг на друга.
    int previousBottom = -1;
    for (int i = 0; i < kFieldCount; ++i) {
        auto *editor = panel.findChild<QComboBox *>(QStringLiteral("schemaField_field%1").arg(i));
        ASSERT_NE(editor, nullptr);

        EXPECT_GE(editor->height(), editor->minimumSizeHint().height()) << "field " << i;

        const QRect rect(editor->mapTo(scroll->widget(), QPoint(0, 0)), editor->size());
        EXPECT_GE(rect.top(), previousBottom) << "field " << i << " overlaps the previous one";
        previousBottom = rect.bottom();
    }
}
