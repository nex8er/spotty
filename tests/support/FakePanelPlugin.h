/**
 * \file FakePanelPlugin.h
 * \brief Тестовый двойник панельного плагина.
 */
#pragma once

#include <spotty/ui/IPanelPlugin.h>

#include <QLabel>

namespace spotty::test {

/**
 * \class FakePanelPlugin
 * \brief Панельный плагин, ничего не делающий, но объявляющий всё, что можно объявить.
 *
 * \par Зачем
 *
 * То же, зачем существует spotty::test::FakeInterfacePlugin: это независимая реализация
 * SDK, написанная не автором четырёх встроенных панелей. Те подгонялись под API по ходу
 * его сочинения и потому доказывают мало. Если API удобно ложится и сюда, значит он не
 * прирос к макросам с журналом.
 *
 * Регистрируется через spotty::PanelPluginRegistry::addBuiltin() — Q_PLUGIN_METADATA у
 * него намеренно нет, раскладывать собранные модули по каталогам ради теста незачем.
 */
class FakePanelPlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_INTERFACES(spotty::IPanelPlugin)

public:
    explicit FakePanelPlugin(QString id = QStringLiteral("fake"))
        : m_id(std::move(id))
    {
    }

    QString pluginId() const override { return m_id; }
    QString displayName() const override { return QStringLiteral("Fake"); }

    QList<PanelDescriptor> panels() const override { return declaredPanels; }

    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override
    {
        Q_UNUSED(host);
        lastCreated = panelId;
        return new QLabel(panelId, parent);
    }

    int uiApiVersion() const override { return apiVersion; }

    SettingsSchema settingsSchema() const override { return schema; }

    /// \brief Что вернёт panels(). Заполняется тестом.
    QList<PanelDescriptor> declaredPanels;

    /// \brief Что вернёт settingsSchema().
    SettingsSchema schema;

    /// \brief Подменяемая версия — чтобы проверить отказ по несовпадению.
    int apiVersion = SPOTTY_UI_API_VERSION;

    /// \brief Идентификатор, с которым в последний раз звали createPanel().
    QString lastCreated;

private:
    QString m_id;
};

/// \brief Панель с заданным идентификатором и порядком; остальное по умолчанию.
inline PanelDescriptor makePanel(const QString &id, int order = 100)
{
    return PanelDescriptor{.id = id, .title = id, .order = order};
}

} // namespace spotty::test
