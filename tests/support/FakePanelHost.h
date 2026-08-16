/**
 * \file FakePanelHost.h
 * \brief Двойник IPanelHost для тестов панелей.
 */
#pragma once

#include <spotty/ui/IPanelHost.h>

#include <QColor>
#include <QDir>
#include <QHash>
#include <QIcon>
#include <QVariant>

namespace spotty::test {

/**
 * \class FakePanelHost
 * \brief Хост, который ничего не показывает, но всё запоминает.
 *
 * \par Зачем он нужен
 *
 * Панель — это проводка: кто на что подписан, что во что записывается, что применяется и
 * когда. Ровно там и живут ошибки, которые не видит ни один тест модели: галочка меняла
 * модель, но не себя; профиль читался, когда рядов ещё не было, и молча не применялся.
 * Обе дошли до пользователя. Без двойника хоста панель нельзя даже создать.
 *
 * Настройки держатся в памяти, каталог данных — временный: тест видит и то, что панель
 * записала в настройки, и файлы, которые она создала.
 */
class FakePanelHost : public IPanelHost
{
public:
    explicit FakePanelHost(QString dataDirectory, QObject *parent = nullptr)
        : IPanelHost(parent)
        , m_dataDir(std::move(dataDirectory))
    {
        QDir().mkpath(m_dataDir);
    }

    /// \name Что тест может спросить и подстроить
    /// @{
    QHash<QString, QVariant> settings;
    QStringList statusMessages;
    QString alias = QStringLiteral("fake");
    /// @}

    QString pluginId() const override { return QStringLiteral("plotter"); }

    void send(const QByteArray &) override {}
    void composeInSendBar(const QString &, DataCodec::Format) override {}

    ChannelState channelState() const override { return ChannelState::Closed; }
    QString interfaceId() const override { return {}; }
    QString interfaceName() const override { return alias; }
    QString interfaceAlias() const override { return alias; }

    QVariant value(const QString &key, const QVariant &fallback = {}) const override
    {
        return settings.value(key, fallback);
    }
    void setValue(const QString &key, const QVariant &value) override
    {
        settings.insert(key, value);
    }

    QString dataDir() const override { return m_dataDir; }
    QString documentsDir() const override { return m_dataDir; }

    void showStatusMessage(const QString &message) override
    {
        statusMessages.append(message);
    }
    void activatePanel(const QString &) override {}
    void setShortcuts(const QList<PanelShortcut> &) override {}

    void appendToTerminal(const QString &) override {}
    void injectReceived(const QByteArray &) override {}
    void clearTerminal() override {}
    bool showDocument(const QString &, const QString &) override { return false; }

    void setHighlightRules(const HighlightRules &) override {}
    void setSearchPattern(const QString &, SearchOptions) override {}
    void setFilterEnabled(bool) override {}
    void findNext() override {}
    void findPrevious() override {}
    int matchCount() const override { return 0; }
    int currentMatch() const override { return 0; }

    qint64 firstLineNumber() const override { return 0; }
    qint64 nextLineNumber() const override { return 0; }
    bool line(qint64, TerminalLine *) const override { return false; }

    // Цвета отдаются осмысленные, а не чёрные: отрисовка панели по ним считает контраст, и
    // сплошной чёрный дал бы неотличимые друг от друга пиксели там, где тест их сравнивает.
    QColor color(ColorRole role) const override
    {
        return role == ColorRole::Base ? QColor(0x20, 0x20, 0x20) : QColor(0xC0, 0xC0, 0xC0);
    }
    int metric(Metric) const override { return 8; }
    bool isDarkTheme() const override { return true; }
    QIcon icon(char32_t, int) const override { return {}; }
    QIcon mutedIcon(char32_t, int) const override { return {}; }

private:
    QString m_dataDir;
};

} // namespace spotty::test
