/**
 * \file test_interface_settings_panel.cpp
 * \brief Тесты раскладки панели настроек интерфейса.
 */
#include "support/FakeInterfacePlugin.h"
#include "support/TestSupport.h"

#include "CollapsibleSection.h"
#include "SchemaForm.h"
#include "dialogs/ActionPromptDialog.h"
#include "dialogs/InterfaceSettingsPanel.h"

#include "AgentPackage.h"
#include "TcpTunnelPlugin.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <settings/SettingsStore.h>

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QPalette>
#include <QImage>
#include <QPixmap>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTcpServer>
#include <QTimer>

#include <gtest/gtest.h>

#include <QFile>

#include <memory>

using namespace spotty;
using spotty::test::FakeInterfacePlugin;
using spotty::test::TempDir;

namespace {

constexpr int kFieldCount = 14;

/**
 * \brief Для снимков: оформить приложение готовой таблицей стилей из `SPOTTY_TEST_QSS`.
 *
 * Без темы снимок показывает голый Fusion, а владелец видит окно с QSS: рамки групп, цвет
 * подсказок и акцентный контур главной кнопки в нём другие. Файл готовит tools/resolve_theme_qss.py;
 * рядом с ним лежит `<файл>.pal` — шесть
 * цветов палитры (окно, текст, поле, панель, акцент, приглушённый). Не задана переменная —
 * ничего не делает, так что обычный прогон тестов остаётся прежним.
 */
void applySnapshotTheme()
{
    static QString applied;
    const QString path = qEnvironmentVariable("SPOTTY_TEST_QSS");
    if (path.isEmpty() || path == applied)
        return;
    QFile sheet(path);
    if (!sheet.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QApplication::setStyle(QStringLiteral("Fusion"));
    // Системный шрифт macOS — 13 pt; у офскрин-платформы он мельче, и подписи второго плана
    // (кегль от базового) выглядели бы крупнее основного текста.
    QFont font = QApplication::font();
    font.setPointSize(13);
    QApplication::setFont(font);
    QFile paletteFile(path + QStringLiteral(".pal"));
    if (paletteFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList colors = QString::fromUtf8(paletteFile.readAll()).split(u' ', Qt::SkipEmptyParts);
        if (colors.size() >= 6) {
            QPalette palette;
            palette.setColor(QPalette::Window, QColor(colors[0]));
            palette.setColor(QPalette::WindowText, QColor(colors[1]));
            palette.setColor(QPalette::Base, QColor(colors[2]));
            palette.setColor(QPalette::Text, QColor(colors[1]));
            palette.setColor(QPalette::Button, QColor(colors[3]));
            palette.setColor(QPalette::ButtonText, QColor(colors[1]));
            palette.setColor(QPalette::Highlight, QColor(colors[4]));
            palette.setColor(QPalette::PlaceholderText, QColor(colors[5]));
            QApplication::setPalette(palette);
        }
    }
    qApp->setStyleSheet(QString::fromUtf8(sheet.readAll()));
    applied = path;
}

/// \brief Сохранить снимок виджета, если задан `SPOTTY_TEST_SNAPSHOT_DIR` (рабочий стол не задет).
void saveSnapshot(QWidget *widget, const QString &name)
{
    const QString dir = qEnvironmentVariable("SPOTTY_TEST_SNAPSHOT_DIR");
    if (dir.isEmpty())
        return;
    widget->grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

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
                .options = {{QStringLiteral("cu.usbmodem0000593034541 - J-Link, a long port description"), 1},
                            {QStringLiteral("two"), 2}},
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
    panel.resize(200, 300);
    panel.show();
    // Полосы прокрутки появляются по отложенному запросу раскладки.
    QCoreApplication::processEvents();

    auto *scroll = panel.findChild<QScrollArea *>();
    ASSERT_NE(scroll, nullptr);
    // Содержимое не сжато ниже своего минимума, а то, что в окно не влезло, доступно
    // горизонтальной прокруткой. Наименьшая ширина зависит от шрифта системы (на Windows она
    // больше, чем на macOS), поэтому проверяется не число, а согласованность.
    const int minimumWidth = scroll->widget()->minimumSizeHint().width();
    EXPECT_GE(scroll->widget()->width(), minimumWidth);
    // Окно сужено заведомо ниже минимума формы на любой системе.
    ASSERT_GT(minimumWidth, scroll->viewport()->width());
    EXPECT_TRUE(scroll->horizontalScrollBar()->isVisible());
    EXPECT_GT(scroll->horizontalScrollBar()->maximum(), 0);

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

namespace {

/// \brief Обвязка «реестр + плагин с устройствами пользователя + панель».
struct UserDeviceFixture
{
    TempDir dir;
    PluginManager plugins;
    FakeInterfacePlugin plugin;
    SettingsStore store;
    InterfaceRegistry registry;
    std::unique_ptr<InterfaceSettingsPanel> panel;

    explicit UserDeviceFixture(bool userDevices = true, bool requireMode = false,
                               bool withPassword = false)
        : store(dir.filePath(QStringLiteral("interfaces.json")))
        , registry(&plugins, &store)
    {
        plugin.userDevices = userDevices;
        plugin.requireMode = requireMode;
        plugin.withPassword = withPassword;
        EXPECT_TRUE(plugins.addPlugin(&plugin));
        store.load();
        panel = std::make_unique<InterfaceSettingsPanel>(&registry, &plugins);
    }

    QPushButton *addButton() { return panel->findChild<QPushButton *>(QStringLiteral("addConnectionButton")); }
    QPushButton *removeButton() { return panel->findChild<QPushButton *>(QStringLiteral("removeConnectionButton")); }
    QComboBox *deviceCombo() { return panel->findChild<QComboBox *>(); }

    /// \brief Нажать кнопку добавления и вернуть идентификатор созданного устройства.
    QString addConnection()
    {
        addButton()->click();
        return deviceCombo()->currentData().toString();
    }
};

/// \brief Ответить «Да» на вопрос, который окно задаст, как только запустит свой цикл.
void answerYesToTheNextQuestion()
{
    QTimer::singleShot(0, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(QMessageBox::Yes)->click();
    });
}

} // namespace

TEST(InterfaceSettingsPanel, NewConnectionButtonExistsOnlyWhereAPluginCreatesDevices)
{
    UserDeviceFixture without(/*userDevices=*/false);
    EXPECT_FALSE(without.addButton()->isVisibleTo(without.panel.get()));
    EXPECT_FALSE(without.removeButton()->isVisibleTo(without.panel.get()));

    UserDeviceFixture with(/*userDevices=*/true);
    EXPECT_TRUE(with.addButton()->isVisibleTo(with.panel.get()));
    EXPECT_TRUE(with.removeButton()->isVisibleTo(with.panel.get()));
}

TEST(InterfaceSettingsPanel, NewConnectionIsCreatedAndSelected)
{
    UserDeviceFixture f;
    const QString id = f.addConnection();

    ASSERT_FALSE(id.isEmpty());
    ASSERT_NE(f.registry.entry(id), nullptr);
    EXPECT_TRUE(f.registry.entry(id)->userDefined);
    EXPECT_TRUE(f.removeButton()->isEnabled());
}

TEST(InterfaceSettingsPanel, RemoveIsOnlyForConnectionsTheUserCreated)
{
    UserDeviceFixture f;
    f.plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"), QStringLiteral("dev-a"))};
    f.registry.refresh();
    f.panel->selectInterface(QStringLiteral("fake:a"));
    EXPECT_FALSE(f.removeButton()->isEnabled()); // обнаруженное устройство удалить нельзя

    const QString id = f.addConnection();
    EXPECT_TRUE(f.removeButton()->isEnabled());

    answerYesToTheNextQuestion();
    f.removeButton()->click();

    EXPECT_EQ(f.registry.entry(id), nullptr);
    EXPECT_NE(f.registry.entry(QStringLiteral("fake:a")), nullptr);
}

TEST(InterfaceSettingsPanel, DecliningTheQuestionKeepsTheConnection)
{
    UserDeviceFixture f;
    const QString id = f.addConnection();

    QTimer::singleShot(0, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(QMessageBox::No)->click();
    });
    f.removeButton()->click();

    EXPECT_NE(f.registry.entry(id), nullptr);
}

TEST(InterfaceSettingsPanel, UnfilledNewConnectionIsDiscardedButFilledOneStays)
{
    UserDeviceFixture f(/*userDevices=*/true, /*requireMode=*/true);

    const QString empty = f.addConnection();
    f.registry.setSettingsFor(empty, {{QStringLiteral("mode"), QString()}}); // обязательное пусто
    const QString filled = f.addConnection();
    f.registry.setSettingsFor(filled, {{QStringLiteral("mode"), QStringLiteral("8N1")}});

    f.panel->discardUnconfigured();

    EXPECT_EQ(f.registry.entry(empty), nullptr);
    EXPECT_NE(f.registry.entry(filled), nullptr);
}

TEST(InterfaceSettingsPanel, ConnectionNotCreatedByThePanelIsNotDiscardedUnlessMarked)
{
    UserDeviceFixture f(/*userDevices=*/true, /*requireMode=*/true);
    const QString id = f.registry.addUserDevice(QStringLiteral("fake"));
    f.registry.setSettingsFor(id, {{QStringLiteral("mode"), QString()}});

    // Старое подключение, у которого поле опустошили руками, молча пропасть не должно.
    f.panel->discardUnconfigured();
    EXPECT_NE(f.registry.entry(id), nullptr);

    // Заведённое пунктом списка и переданное панели — отбрасывается так же, как своё.
    f.panel->markCreated(id);
    f.panel->discardUnconfigured();
    EXPECT_EQ(f.registry.entry(id), nullptr);
}

TEST(InterfaceSettingsPanel, PasswordFieldIsMaskedAndHeldOnlyInMemory)
{
    UserDeviceFixture f(/*userDevices=*/true, /*requireMode=*/false, /*withPassword=*/true);
    const QString id = f.addConnection();

    auto *edit = f.panel->findChild<QLineEdit *>(QStringLiteral("schemaField_secret"));
    ASSERT_NE(edit, nullptr);
    EXPECT_EQ(edit->echoMode(), QLineEdit::Password);

    edit->setText(QStringLiteral("hunter2"));
    Q_EMIT edit->editingFinished();

    EXPECT_EQ(f.registry.settingsFor(id).value(QStringLiteral("secret")).toString(),
              QStringLiteral("hunter2"));
    f.store.save();
    QFile file(f.dir.filePath(QStringLiteral("interfaces.json")));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_FALSE(file.readAll().contains("hunter2"));
}

namespace {

/// \brief Плагин с редактируемым списком, как у скорости UART: пункты есть, но можно набрать своё.
class EditableChoicePlugin : public FakeInterfacePlugin
{
public:
    SettingsSchema settingsSchema() const override
    {
        SettingsSchema schema;
        schema.add(SettingsField{
            .key = QStringLiteral("rate"),
            .label = QStringLiteral("Rate"),
            .type = SettingsField::Choice,
            .defaultValue = 9600,
            .options = {{QStringLiteral("9600"), 9600}, {QStringLiteral("115200"), 115200}},
            .editable = true,
        });
        schema.add(SettingsField{
            .key = QStringLiteral("name"),
            .label = QStringLiteral("Name"),
            .type = SettingsField::Choice,
            .defaultValue = QString(),
            .options = {{QStringLiteral("First one (a)"), QStringLiteral("a")},
                        {QStringLiteral("Second one (b)"), QStringLiteral("b")}},
            .editable = true,
        });
        return schema;
    }
};

} // namespace

TEST(InterfaceSettingsPanel, TypedTextInAnEditableListBeatsTheItemSelectedBefore)
{
    TempDir dir;
    PluginManager plugins;
    EditableChoicePlugin plugin;
    SettingsStore store(dir.filePath(QStringLiteral("interfaces.json")));
    InterfaceRegistry registry(&plugins, &store);
    ASSERT_TRUE(plugins.addPlugin(&plugin));
    store.load();
    plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"), QStringLiteral("dev-a"))};
    registry.refresh();

    InterfaceSettingsPanel panel(&registry, &plugins);
    panel.selectInterface(QStringLiteral("fake:a"));

    auto *rate = panel.findChild<QComboBox *>(QStringLiteral("schemaField_rate"));
    auto *name = panel.findChild<QComboBox *>(QStringLiteral("schemaField_name"));
    ASSERT_NE(rate, nullptr);
    ASSERT_NE(name, nullptr);

    // Выбрали пункт, потом набрали своё: правка должна победить, а не уступить пункту,
    // выбранному раньше (currentIndex() при вводе не меняется).
    rate->setCurrentIndex(1);
    EXPECT_EQ(registry.settingsFor(QStringLiteral("fake:a")).value(QStringLiteral("rate")).toInt(), 115200);
    rate->lineEdit()->setText(QStringLiteral("250000"));
    EXPECT_EQ(registry.settingsFor(QStringLiteral("fake:a")).value(QStringLiteral("rate")).toInt(), 250000);

    // Тот же случай, когда подпись пункта не совпадает со значением («First one (a)» → «a»).
    name->setCurrentIndex(0);
    EXPECT_EQ(registry.settingsFor(QStringLiteral("fake:a")).value(QStringLiteral("name")).toString(),
              QStringLiteral("a"));
    name->lineEdit()->setText(QStringLiteral("10.0.0.5"));
    EXPECT_EQ(registry.settingsFor(QStringLiteral("fake:a")).value(QStringLiteral("name")).toString(),
              QStringLiteral("10.0.0.5"));
}

namespace {

/// \brief Настоящий плагин tcptunnel (ssh — заглушка, комплект агента — каталоги исходников) и его реестр.
struct TunnelFixture
{
    TempDir dir;
    PluginManager plugins;
    TcpTunnelPlugin plugin;
    SettingsStore store{dir.filePath(QStringLiteral("interfaces.json"))};
    InterfaceRegistry registry{&plugins, &store};

    TunnelFixture()
    {
        plugin.setSshForTesting([]() -> std::unique_ptr<tcptunnel::ISshSession> { return nullptr; },
                                std::make_shared<tcptunnel::AgentPackage>(
                                    QStringLiteral(SPOTTY_AGENT_BINARIES_DIR), QStringLiteral(SPOTTY_AGENT_DIR)));
        EXPECT_TRUE(plugins.addPlugin(&plugin));
        store.load();
    }
};

} // namespace

TEST(InterfaceSettingsPanel, ScanResultsFillTheHostAndPortListsOfATunnel)
{
    // Единственный путь «подставить найденное в форму»: адрес и порт — редактируемые списки с
    // живыми пунктами. Проверяется вместе с настоящей панелью: ни схема, ни сканер по
    // отдельности не покажут, что выбор из найденного доходит до настроек.
    QTcpServer agent;
    ASSERT_TRUE(agent.listen(QHostAddress::LocalHost, 0));

    TunnelFixture f;
    auto &registry = f.registry;
    InterfaceSettingsPanel panel(&registry, &f.plugins);
    panel.show(); // таймер живых полей идёт только у видимой панели
    panel.findChild<QPushButton *>(QStringLiteral("addConnectionButton"))->click();
    const QString id = panel.findChild<QComboBox *>()->currentData().toString();
    ASSERT_FALSE(id.isEmpty());

    auto *range = panel.findChild<QComboBox *>(QStringLiteral("schemaField_scanRange"));
    auto *firstPort = panel.findChild<QSpinBox *>(QStringLiteral("schemaField_scanPort"));
    auto *scan = panel.findChild<QPushButton *>(QStringLiteral("schemaField_scan"));
    ASSERT_NE(range, nullptr);
    ASSERT_NE(firstPort, nullptr);
    ASSERT_NE(scan, nullptr);

    range->lineEdit()->setText(QStringLiteral("127.0.0.1")); // свой диапазон набирают руками
    firstPort->setValue(agent.serverPort());
    scan->click();

    auto host = [&] { return panel.findChild<QComboBox *>(QStringLiteral("schemaField_host")); };
    ASSERT_TRUE(spotty::test::waitFor(
        [&] { return host() && host()->findData(QStringLiteral("127.0.0.1")) >= 0; }, 5000));

    // Выбор найденного хоста попадает в настройки значением (адресом), а не подписью со списком портов.
    host()->setCurrentIndex(host()->findData(QStringLiteral("127.0.0.1")));
    EXPECT_EQ(registry.settingsFor(id).value(QStringLiteral("host")).toString(), QStringLiteral("127.0.0.1"));
    // …и подпись подключения в списке интерфейсов обновилась.
    EXPECT_EQ(registry.entry(id)->descriptor.systemName,
              QStringLiteral("127.0.0.1:7777")); // порт ещё не выбран: умолчание

    // Теперь для этого хоста предложен найденный порт.
    auto port = [&] { return panel.findChild<QComboBox *>(QStringLiteral("schemaField_port")); };
    ASSERT_TRUE(spotty::test::waitFor([&] { return port() && port()->findData(int(agent.serverPort())) >= 0; }, 5000));
    port()->setCurrentIndex(port()->findData(int(agent.serverPort())));
    EXPECT_EQ(registry.settingsFor(id).value(QStringLiteral("port")).toInt(), int(agent.serverPort()));
    EXPECT_EQ(registry.entry(id)->descriptor.systemName,
              QStringLiteral("127.0.0.1:%1").arg(agent.serverPort()));
}

// --- Раскладка: компактные поля и «Advanced» -------------------------------------------

namespace {

/// \brief Панель с подставным плагином и одним найденным устройством.
struct LayoutFixture
{
    TempDir dir;
    PluginManager plugins;
    FakeInterfacePlugin plugin;
    SettingsStore store;
    InterfaceRegistry registry;
    std::unique_ptr<InterfaceSettingsPanel> panel;

    explicit LayoutFixture(bool layout, bool action = false, bool prompt = false)
        : store(dir.filePath(QStringLiteral("interfaces.json")))
        , registry(&plugins, &store)
    {
        plugin.withLayoutFields = layout;
        plugin.withAction = action;
        plugin.withPrompt = prompt;
        EXPECT_TRUE(plugins.addPlugin(&plugin));
        store.load();
        plugin.devices = {FakeInterfacePlugin::makeDevice(QStringLiteral("a"), QStringLiteral("dev-a"))};
        registry.refresh();
        panel = std::make_unique<InterfaceSettingsPanel>(&registry, &plugins);
        panel->selectInterface(QStringLiteral("fake:a"));
    }

    template <typename T>
    T *editor(const QString &key) { return panel->findChild<T *>(QStringLiteral("schemaField_") + key); }
};

} // namespace

TEST(InterfaceSettingsPanel, CompactFieldStandsOnTheSameRowRightOfThePreviousOne)
{
    LayoutFixture f(/*layout=*/true);
    auto *mode = f.editor<QLineEdit>(QStringLiteral("mode"));
    auto *narrow = f.editor<QSpinBox>(QStringLiteral("narrow"));
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(narrow, nullptr);

    // Один контейнер строки, а не две строки формы друг под другом.
    EXPECT_EQ(mode->parentWidget(), narrow->parentWidget());
    EXPECT_LE(narrow->maximumWidth(), 120);
    EXPECT_GT(narrow->maximumWidth(), 0);

    // Подпись узкого поля — рядом с ним, в той же строке.
    bool labelInRow = false;
    for (QLabel *label : narrow->parentWidget()->findChildren<QLabel *>())
        labelInRow = labelInRow || label->text() == QStringLiteral("Narrow");
    EXPECT_TRUE(labelInRow);

    // Пояснение узкого поля — всплывающая подсказка, а не строка под полем: под соседом оно
    // выглядело бы пояснением к нему.
    EXPECT_EQ(narrow->toolTip(), QStringLiteral("narrow hint"));
    for (QLabel *label : f.panel->findChildren<QLabel *>(QStringLiteral("hintLabel")))
        EXPECT_NE(label->text(), QStringLiteral("narrow hint"));

    // Значение узкого поля применяется, как у любого другого.
    narrow->setValue(42);
    EXPECT_EQ(f.registry.settingsFor(QStringLiteral("fake:a")).value(QStringLiteral("narrow")).toInt(), 42);
}

TEST(InterfaceSettingsPanel, WithoutAnyCompactFieldEveryFieldKeepsItsOwnRow)
{
    LayoutFixture f(/*layout=*/false);
    auto *speed = f.editor<QSpinBox>(QStringLiteral("speed"));
    auto *mode = f.editor<QLineEdit>(QStringLiteral("mode"));
    ASSERT_NE(speed, nullptr);
    ASSERT_NE(mode, nullptr);
    // Рядом они стоят в общей форме группы, но не в одном контейнере строки.
    EXPECT_EQ(speed->parentWidget(), mode->parentWidget()); // оба — прямые дети группы
    EXPECT_EQ(speed->maximumWidth(), QWIDGETSIZE_MAX);
    EXPECT_EQ(mode->maximumWidth(), QWIDGETSIZE_MAX);
}

TEST(InterfaceSettingsPanel, RareFieldsAreHiddenInACollapsedAdvancedSection)
{
    LayoutFixture f(/*layout=*/true);
    auto *rare = f.editor<QCheckBox>(QStringLiteral("rare"));
    auto *section = f.panel->findChild<CollapsibleSection *>(QStringLiteral("advancedSection"));
    ASSERT_NE(rare, nullptr);
    ASSERT_NE(section, nullptr);

    // Свёрнуто: поля нет на экране, но оно есть, и его значение читается и пишется.
    EXPECT_FALSE(section->isExpanded());
    EXPECT_FALSE(rare->isVisibleTo(f.panel.get()));
    EXPECT_FALSE(f.registry.settingsFor(QStringLiteral("fake:a")).value(QStringLiteral("rare")).toBool());

    section->setExpanded(true);
    EXPECT_TRUE(rare->isVisibleTo(f.panel.get()));
    rare->setChecked(true);
    EXPECT_TRUE(f.registry.settingsFor(QStringLiteral("fake:a")).value(QStringLiteral("rare")).toBool());

    // Заголовок раздела один на группу, а обычные поля остались вне него.
    EXPECT_EQ(f.panel->findChildren<CollapsibleSection *>().size(), 1);
    EXPECT_TRUE(f.editor<QLineEdit>(QStringLiteral("mode"))->isVisibleTo(f.panel.get()));
}

// --- Окно вопроса перед действием ------------------------------------------------------

namespace {

/// \brief Заполнить и закрыть окно вопроса, как только оно откроется. `answer` получает окно.
void answerPrompt(const std::function<void(ActionPromptDialog *)> &answer)
{
    QTimer::singleShot(0, [answer] {
        auto *dialog = qobject_cast<ActionPromptDialog *>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        answer(dialog);
    });
}

QLineEdit *passwordEdit(QWidget *root)
{
    for (QLineEdit *edit : root->findChildren<QLineEdit *>()) {
        if (edit->echoMode() == QLineEdit::Password)
            return edit;
    }
    return nullptr;
}

QLineEdit *textEdit(QWidget *root, int index)
{
    int seen = 0;
    for (QLineEdit *edit : root->findChildren<QLineEdit *>()) {
        if (edit->echoMode() == QLineEdit::Normal && seen++ == index)
            return edit;
    }
    return nullptr;
}

} // namespace

TEST(InterfaceSettingsPanel, ActionWithAPromptAsksFirstAndPassesTheAnswerToThePlugin)
{
    LayoutFixture f(/*layout=*/false, /*action=*/true, /*prompt=*/true);
    auto *button = f.editor<QPushButton>(QStringLiteral("act"));
    ASSERT_NE(button, nullptr);

    answerPrompt([](ActionPromptDialog *dialog) {
        EXPECT_EQ(dialog->windowTitle(), QStringLiteral("Do it"));
        textEdit(dialog, 0)->setText(QStringLiteral("pi"));
        passwordEdit(dialog)->setText(QStringLiteral("hunter2"));
        dialog->accept();
    });
    button->click();

    // Плагин получил настройки устройства и добавленный ответ.
    EXPECT_EQ(f.plugin.actionCount, 1);
    EXPECT_EQ(f.plugin.lastActionKey, QStringLiteral("act"));
    EXPECT_EQ(f.plugin.lastActionSettings.value(QStringLiteral("user")).toString(), QStringLiteral("pi"));
    EXPECT_EQ(f.plugin.lastActionSettings.value(QStringLiteral("pass")).toString(), QStringLiteral("hunter2"));
    EXPECT_TRUE(f.plugin.lastActionSettings.contains(QStringLiteral("speed"))); // и обычные настройки

    // А в настройки устройства ответ не попал и на диск не уйдёт.
    const QVariantMap stored = f.registry.settingsFor(QStringLiteral("fake:a"));
    EXPECT_FALSE(stored.contains(QStringLiteral("user")));
    EXPECT_FALSE(stored.contains(QStringLiteral("pass")));
    f.store.save();
    QFile file(f.dir.filePath(QStringLiteral("interfaces.json")));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray contents = file.readAll();
    EXPECT_FALSE(contents.contains("hunter2"));
    EXPECT_FALSE(contents.contains("\"pi\""));
}

TEST(InterfaceSettingsPanel, CancellingThePromptRunsNothing)
{
    LayoutFixture f(false, true, true);
    answerPrompt([](ActionPromptDialog *dialog) { dialog->reject(); });

    f.editor<QPushButton>(QStringLiteral("act"))->click();

    EXPECT_EQ(f.plugin.actionCount, 0);
}

TEST(InterfaceSettingsPanel, ThePromptRemembersTheAnswerForThisWindowOnly)
{
    LayoutFixture f(false, true, true);
    answerPrompt([](ActionPromptDialog *dialog) {
        textEdit(dialog, 0)->setText(QStringLiteral("pi"));
        passwordEdit(dialog)->setText(QStringLiteral("hunter2"));
        dialog->accept();
    });
    f.editor<QPushButton>(QStringLiteral("act"))->click();

    // Три действия подряд не должны требовать трёх паролей: поля уже заполнены.
    answerPrompt([](ActionPromptDialog *dialog) {
        EXPECT_EQ(textEdit(dialog, 0)->text(), QStringLiteral("pi"));
        EXPECT_EQ(passwordEdit(dialog)->text(), QStringLiteral("hunter2"));
        dialog->accept();
    });
    f.editor<QPushButton>(QStringLiteral("act"))->click();
    EXPECT_EQ(f.plugin.actionCount, 2);

    // Но живёт это только в панели: новая панель начинает с пустых полей.
    InterfaceSettingsPanel another(&f.registry, &f.plugins);
    another.selectInterface(QStringLiteral("fake:a"));
    answerPrompt([](ActionPromptDialog *dialog) {
        EXPECT_TRUE(textEdit(dialog, 0)->text().isEmpty());
        EXPECT_TRUE(passwordEdit(dialog)->text().isEmpty());
        dialog->reject();
    });
    another.findChild<QPushButton *>(QStringLiteral("schemaField_act"))->click();
}

TEST(InterfaceSettingsPanel, ActionWithoutAPromptRunsAtOnce)
{
    LayoutFixture f(false, /*action=*/true, /*prompt=*/false);
    f.editor<QPushButton>(QStringLiteral("act"))->click();
    EXPECT_EQ(f.plugin.actionCount, 1);
}

TEST(ActionPromptDialogTest, OkWaitsForTheRequiredFieldsAndKeyFileIsHiddenUnderAdvanced)
{
    FakeInterfacePlugin plugin;
    plugin.withPrompt = true;
    const SettingsSchema schema = plugin.promptSchema(QStringLiteral("act"), {});
    ActionPromptDialog dialog(QStringLiteral("Title"), schema, {});

    auto *ok = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    ASSERT_NE(ok, nullptr);
    EXPECT_FALSE(ok->isEnabled()); // имя пользователя обязательно и пока пусто

    textEdit(&dialog, 0)->setText(QStringLiteral("pi"));
    EXPECT_TRUE(ok->isEnabled());
    textEdit(&dialog, 0)->setText(QStringLiteral("   "));
    EXPECT_FALSE(ok->isEnabled()); // пробелы — не имя

    // Пароль скрыт, а файл ключа — в свёрнутом разделе.
    ASSERT_NE(passwordEdit(&dialog), nullptr);
    auto *section = dialog.findChild<CollapsibleSection *>();
    ASSERT_NE(section, nullptr);
    EXPECT_FALSE(section->isExpanded());
    QLineEdit *keyFile = textEdit(&dialog, 1);
    ASSERT_NE(keyFile, nullptr);
    EXPECT_FALSE(keyFile->isVisibleTo(&dialog));
    section->setExpanded(true);
    EXPECT_TRUE(keyFile->isVisibleTo(&dialog));
}

TEST(ActionPromptDialogTest, ValuesHoldOnlyTheKeysOfTheSchema)
{
    FakeInterfacePlugin plugin;
    plugin.withPrompt = true;
    const SettingsSchema schema = plugin.promptSchema(QStringLiteral("act"), {});
    // Начальные значения могут нести и чужие ключи (например, весь ввод прошлого окна): наружу
    // они не должны утекать.
    ActionPromptDialog dialog(QStringLiteral("T"), schema,
                              {{QStringLiteral("user"), QStringLiteral("pi")}, {QStringLiteral("stranger"), 1}});

    const QVariantMap values = dialog.values();
    EXPECT_EQ(values.value(QStringLiteral("user")).toString(), QStringLiteral("pi"));
    EXPECT_FALSE(values.contains(QStringLiteral("stranger")));
    EXPECT_TRUE(values.contains(QStringLiteral("pass")));
    EXPECT_TRUE(values.contains(QStringLiteral("keyfile")));
}

TEST(ActionPromptDialogTest, CursorGoesToTheFirstEmptyVisibleField)
{
    FakeInterfacePlugin plugin;
    plugin.withPrompt = true;
    const SettingsSchema schema = plugin.promptSchema(QStringLiteral("act"), {});
    ActionPromptDialog dialog(QStringLiteral("T"), schema, {{QStringLiteral("user"), QStringLiteral("pi")}});
    dialog.show();
    QCoreApplication::processEvents();

    // Имя уже стоит от прошлого раза, ждут пароль; скрытый файл ключа не получает фокус.
    EXPECT_TRUE(passwordEdit(&dialog)->hasFocus());
}

TEST(SchemaFormTest, PasswordFieldsAreOnlyShownWhenTheCallerKeepsNothingOnDisk)
{
    FakeInterfacePlugin plugin;
    plugin.withPassword = true;
    const SettingsSchema schema = plugin.settingsSchema();

    // Настройки панелей лежат в файле: поле пароля там обещало бы то, чего не будет.
    SchemaForm panelForm(schema, {});
    int panelMasked = 0;
    for (QLineEdit *edit : panelForm.findChildren<QLineEdit *>())
        panelMasked += edit->echoMode() == QLineEdit::Password;
    EXPECT_EQ(panelMasked, 0);

    SchemaForm promptForm(schema, {}, nullptr, /*allowSecrets=*/true);
    int masked = 0;
    for (QLineEdit *edit : promptForm.findChildren<QLineEdit *>())
        masked += edit->echoMode() == QLineEdit::Password;
    EXPECT_EQ(masked, 1);
}

TEST(SchemaFormTest, AdvancedFieldsAreCollapsedButTheirValuesAreCollected)
{
    FakeInterfacePlugin plugin;
    plugin.withLayoutFields = true;
    const SettingsSchema schema = plugin.settingsSchema();
    SchemaForm form(schema, {{QStringLiteral("rare"), true}});

    auto *section = form.findChild<CollapsibleSection *>();
    ASSERT_NE(section, nullptr);
    EXPECT_FALSE(section->isExpanded());
    // Спрятанное поле живёт: его начальное значение подставлено и возвращается values().
    EXPECT_TRUE(form.values().value(QStringLiteral("rare")).toBool());
}

// --- Диалог настроек туннеля: как он выглядит на самом деле ----------------------------

namespace {

/// \brief Положение виджета в координатах панели.
QRect rectIn(const QWidget *panel, const QWidget *widget)
{
    return QRect(widget->mapTo(panel, QPoint(0, 0)), widget->size());
}

} // namespace

TEST(InterfaceSettingsPanel, TunnelDialogPutsThePortBesideTheAddressAndKeepsItNarrow)
{
    applySnapshotTheme();
    TunnelFixture f;
    const QString id = f.registry.addUserDevice(QStringLiteral("tcptunnel"));

    InterfaceSettingsPanel panel(&f.registry, &f.plugins);
    panel.selectInterface(id);
    panel.resize(680, 1000);
    panel.show();
    QCoreApplication::processEvents();

    auto widget = [&](const QString &key) { return panel.findChild<QWidget *>(QStringLiteral("schemaField_") + key); };
    for (const auto &pair : {std::pair{"host", "port"}, std::pair{"scanRange", "scanPort"}, std::pair{"baud", "format"}}) {
        QWidget *wide = widget(QString::fromLatin1(pair.first));
        QWidget *narrow = widget(QString::fromLatin1(pair.second));
        ASSERT_NE(wide, nullptr) << pair.first;
        ASSERT_NE(narrow, nullptr) << pair.second;
        const QRect wideRect = rectIn(&panel, wide);
        const QRect narrowRect = rectIn(&panel, narrow);

        // В той же строке, правее, и узкое: порт не должен занимать столько же места, сколько адрес.
        EXPECT_GE(narrowRect.left(), wideRect.right()) << pair.second << " is not to the right of " << pair.first;
        EXPECT_LT(qAbs(narrowRect.center().y() - wideRect.center().y()), wideRect.height())
            << pair.second << " is not on the row of " << pair.first;
        EXPECT_LE(narrowRect.width(), 130) << pair.second;
        EXPECT_GT(wideRect.width(), 2 * narrowRect.width()) << pair.first;
    }

    // Редкое спрятано: поля из «Advanced» не на экране, пока раздел свёрнут.
    for (const char *key : {"autoReconnect", "connectTimeoutMs", "flow", "bind", "allow", "forgetKey"})
        EXPECT_FALSE(widget(QString::fromLatin1(key))->isVisibleTo(&panel)) << key;
    // А нужное каждый раз — на виду.
    for (const char *key : {"host", "port", "scanRange", "scanPort", "remoteUart", "baud", "format", "autostart"})
        EXPECT_TRUE(widget(QString::fromLatin1(key))->isVisibleTo(&panel)) << key;

    // Поля из разных групп начинаются с одного левого края: у каждой группы своя форма, и без общей
    // ширины колонки подписей «Host» и «Serial port» уводили бы поля в разные стороны.
    const int fieldEdge = rectIn(&panel, widget(QStringLiteral("host"))).left();
    for (const char *key : {"scanRange", "remoteUart", "baud"})
        EXPECT_EQ(rectIn(&panel, widget(QString::fromLatin1(key))).left(), fieldEdge) << key;

    // Кнопка поиска портов — справа от списка, который она заполняет, и не растянута на строку.
    {
        const QRect list = rectIn(&panel, widget(QStringLiteral("remoteUart")));
        const QRect find = rectIn(&panel, widget(QStringLiteral("findPorts")));
        EXPECT_GE(find.left(), list.right());
        EXPECT_LT(qAbs(find.center().y() - list.center().y()), list.height());
        EXPECT_GT(list.width(), find.width());
    }

    // Установка и удаление — один ряд кнопок природной ширины, а не две строки на всю ширину формы.
    {
        const QRect install = rectIn(&panel, widget(QStringLiteral("install")));
        const QRect uninstall = rectIn(&panel, widget(QStringLiteral("uninstall")));
        EXPECT_GE(uninstall.left(), install.right());
        EXPECT_LT(qAbs(uninstall.center().y() - install.center().y()), install.height());
        EXPECT_LT(install.width(), rectIn(&panel, widget(QStringLiteral("remoteUart"))).width());
        EXPECT_EQ(install.left(), fieldEdge);
        // Запуск поиска сети тоже не на всю ширину: он не в ряду с полями, но и не тянется за ними.
        EXPECT_LT(rectIn(&panel, widget(QStringLiteral("scan"))).width(),
                  rectIn(&panel, widget(QStringLiteral("scanRange"))).width());
    }

    // Главное действие группы помечено для таблицы стилей, второстепенное — нет.
    EXPECT_TRUE(widget(QStringLiteral("install"))->property("primary").toBool());
    EXPECT_TRUE(widget(QStringLiteral("scan"))->property("primary").toBool());
    EXPECT_FALSE(widget(QStringLiteral("uninstall"))->property("primary").toBool());
    EXPECT_FALSE(widget(QStringLiteral("findPorts"))->property("primary").toBool());

    // Флажок подписан сам: нажимается и по тексту, а длинная подпись не раздувает колонку подписей.
    auto *autostart = qobject_cast<QCheckBox *>(widget(QStringLiteral("autostart")));
    ASSERT_NE(autostart, nullptr);
    EXPECT_EQ(autostart->text(), QStringLiteral("Start at boot"));

    // Подключение, заведённое пользователем, не повторяет свою подпись строкой «Address».
    for (QLabel *label : panel.findChildren<QLabel *>()) {
        if (label->text() == QStringLiteral("Address"))
            EXPECT_FALSE(label->isVisibleTo(&panel));
    }

    // Для глаза: SPOTTY_TEST_SNAPSHOT_DIR=/куда → PNG диалога (офскрин-платформа, рабочий стол не задет).
    saveSnapshot(&panel, QStringLiteral("tunnel-settings"));
    // Те же группы с раскрытыми «Advanced».
    for (CollapsibleSection *section : panel.findChildren<CollapsibleSection *>())
        section->setExpanded(true);
    QCoreApplication::processEvents();
    saveSnapshot(&panel, QStringLiteral("tunnel-settings-advanced"));
}

TEST(InterfaceSettingsPanel, ScanOutcomeIsShownWithAnIconAndASeverityForTheStyleSheet)
{
    // Цвет итога не должен быть единственным признаком: перед текстом стоит значок, а свойство
    // `severity` — то, за что цепляется таблица стилей. Проверяется настоящим поиском через
    // плагин и панель: ни одно звено отдельно не покажет, что значок доходит до экрана.
    QTcpServer agent;
    ASSERT_TRUE(agent.listen(QHostAddress::LocalHost, 0));

    TunnelFixture f;
    InterfaceSettingsPanel panel(&f.registry, &f.plugins);
    panel.show();
    panel.findChild<QPushButton *>(QStringLiteral("addConnectionButton"))->click();

    auto *status = panel.findChild<QLabel *>(QStringLiteral("schemaField_scanStatus"));
    auto *range = panel.findChild<QComboBox *>(QStringLiteral("schemaField_scanRange"));
    auto *firstPort = panel.findChild<QSpinBox *>(QStringLiteral("schemaField_scanPort"));
    auto *scan = panel.findChild<QPushButton *>(QStringLiteral("schemaField_scan"));
    ASSERT_NE(status, nullptr);
    ASSERT_NE(range, nullptr);
    ASSERT_NE(firstPort, nullptr);
    ASSERT_NE(scan, nullptr);

    // До поиска — нейтральный текст по умолчанию, без значка.
    EXPECT_EQ(status->text(), QStringLiteral("Not scanned yet."));
    EXPECT_TRUE(status->property("severity").toByteArray().isEmpty());

    range->lineEdit()->setText(QStringLiteral("127.0.0.1"));
    firstPort->setValue(agent.serverPort());
    scan->click();
    ASSERT_TRUE(spotty::test::waitFor([&] { return status->text().contains(QStringLiteral("finished")); }, 5000));
    EXPECT_TRUE(status->text().startsWith(QStringLiteral("\u2713 "))) << qPrintable(status->text());
    EXPECT_EQ(status->property("severity").toByteArray(), QByteArray("ok"));

    // Порт, на котором никто не слушает: «ничего не найдено» — предупреждение, а не успех.
    firstPort->setValue(1);
    scan->click();
    ASSERT_TRUE(spotty::test::waitFor([&] { return status->text().contains(QStringLiteral("Nothing found")); }, 5000));
    EXPECT_TRUE(status->text().startsWith(QStringLiteral("\u26A0 "))) << qPrintable(status->text());
    EXPECT_EQ(status->property("severity").toByteArray(), QByteArray("warning"));

    saveSnapshot(&panel, QStringLiteral("tunnel-scan-warning"));
}

TEST(InterfaceSettingsPanel, SshPromptLooksLikeAShortFormWithTheRestUnderAdvanced)
{
    applySnapshotTheme();
    TunnelFixture f;
    const SettingsSchema prompt = f.plugin.promptSchema(QStringLiteral("install"),
                                                      {{QStringLiteral("host"), QStringLiteral("raspberrypi")}});
    ActionPromptDialog dialog(QStringLiteral("Install or update the agent..."), prompt,
                              {{QStringLiteral("sshUser"), QStringLiteral("pi")}});
    dialog.show();
    QCoreApplication::processEvents();

    // Видны только имя и пароль (и пояснение); ключ, его пароль и порт — под «Advanced».
    int visibleEdits = 0;
    for (QLineEdit *edit : dialog.findChildren<QLineEdit *>()) {
        if (edit->isVisibleTo(&dialog) && !edit->parentWidget()->inherits("QSpinBox"))
            ++visibleEdits;
    }
    EXPECT_EQ(visibleEdits, 2);

    saveSnapshot(&dialog, QStringLiteral("ssh-prompt"));
    // Тот же диалог с раскрытым «Advanced».
    if (auto *section = dialog.findChild<CollapsibleSection *>()) {
        section->setExpanded(true);
        QCoreApplication::processEvents();
        // Окно выросло вслед за раскрытым разделом: иначе поля сжались бы ниже минимума и налезли друг на друга.
        QCoreApplication::processEvents();
        EXPECT_GE(dialog.height(), dialog.sizeHint().height());
        saveSnapshot(&dialog, QStringLiteral("ssh-prompt-advanced"));
    }
}
