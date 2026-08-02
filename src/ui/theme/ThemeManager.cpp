/**
 * \file ThemeManager.cpp
 * \brief Реализация spotty::ThemeManager.
 */
#include "ThemeManager.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>

#include <algorithm>

namespace spotty {

namespace {

/// \brief Первый созданный экземпляр; возвращается из ThemeManager::instance().
ThemeManager *g_instance = nullptr;

/**
 * \brief Цвета тёмной темы.
 *
 * Оформление аскетичное и монохромное: нейтральные серые, никаких градиентов, единственный
 * приглушённый синий акцент — только для фокуса и выделения.
 */
ThemeColors darkColors()
{
    ThemeColors c;
    c.window = QColor(0x1e, 0x20, 0x22);
    c.panel = QColor(0x26, 0x28, 0x2b);
    c.base = QColor(0x17, 0x19, 0x1a);
    c.border = QColor(0x35, 0x38, 0x3c);
    c.text = QColor(0xd8, 0xda, 0xdc);
    c.textMuted = QColor(0x81, 0x86, 0x8c);
    c.accent = QColor(0x4f, 0x9d, 0xd9);
    c.accentText = QColor(0xff, 0xff, 0xff);
    c.selection = QColor(0x2f, 0x4c, 0x62);
    c.rxText = QColor(0xd8, 0xda, 0xdc);
    c.txText = QColor(0x7f, 0xa8, 0x5c);
    c.errorText = QColor(0xd2, 0x6b, 0x6b);
    c.okText = QColor(0x6f, 0xa8, 0x6f);
    return c;
}

/// \brief Цвета светлой темы. Те же роли, тот же принцип.
ThemeColors lightColors()
{
    ThemeColors c;
    c.window = QColor(0xf4, 0xf5, 0xf6);
    c.panel = QColor(0xea, 0xec, 0xee);
    c.base = QColor(0xff, 0xff, 0xff);
    c.border = QColor(0xd0, 0xd3, 0xd7);
    c.text = QColor(0x1d, 0x1f, 0x21);
    c.textMuted = QColor(0x6b, 0x70, 0x76);
    c.accent = QColor(0x1f, 0x6f, 0xb2);
    c.accentText = QColor(0xff, 0xff, 0xff);
    c.selection = QColor(0xbe, 0xd8, 0xef);
    c.rxText = QColor(0x1d, 0x1f, 0x21);
    c.txText = QColor(0x3d, 0x6b, 0x22);
    c.errorText = QColor(0xb3, 0x33, 0x33);
    c.okText = QColor(0x2e, 0x7d, 0x32);
    return c;
}

/**
 * \brief Построить QPalette из набора цветов.
 *
 * Палитра нужна помимо таблицы стилей: её читают виджеты, рисующие себя сами, и стандартные
 * диалоги, до которых QSS не дотягивается.
 */
QPalette buildPalette(const ThemeColors &c)
{
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.panel);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.panel);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, c.accentText);
    p.setColor(QPalette::ToolTipBase, c.panel);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.textMuted);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.border);

    p.setColor(QPalette::Disabled, QPalette::Text, c.textMuted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.textMuted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.textMuted);
    return p;
}

/**
 * \brief Подставить в таблицу стилей значения цветов вместо плейсхолдеров `@имя`.
 * \param sheet Исходный текст QSS.
 * \param c Цвета текущей темы.
 * \return Готовую таблицу стилей.
 */
QString resolveStylesheet(QString sheet, const ThemeColors &c)
{
    QList<QPair<QString, QColor>> substitutions = {
        {QStringLiteral("@window"), c.window},
        {QStringLiteral("@panel"), c.panel},
        {QStringLiteral("@base"), c.base},
        {QStringLiteral("@border"), c.border},
        {QStringLiteral("@text"), c.text},
        {QStringLiteral("@textMuted"), c.textMuted},
        {QStringLiteral("@accent"), c.accent},
        {QStringLiteral("@accentText"), c.accentText},
        {QStringLiteral("@selection"), c.selection},
    };

    // Сначала длинные: подстановка @text раньше @textMuted превратила бы второй в
    // «#d8dadcMuted», и разборщик CSS молча отбросил бы такое правило.
    std::sort(substitutions.begin(), substitutions.end(),
              [](const auto &lhs, const auto &rhs) {
                  return lhs.first.size() > rhs.first.size();
              });

    for (const auto &[placeholder, color] : substitutions)
        sheet.replace(placeholder, color.name(QColor::HexRgb));

    // Оставшийся @имя — опечатка в таблице стилей. Без проверки это был бы цвет, который
    // просто ничего не делает. Комментарии исключаем: в самом QSS написано про
    // «@placeholders».
    static const QRegularExpression comments(QStringLiteral("/\\*.*?\\*/"),
                                             QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression leftover(QStringLiteral("@[A-Za-z][A-Za-z0-9]*"));

    const QRegularExpressionMatch match = leftover.match(QString(sheet).remove(comments));
    if (match.hasMatch())
        qWarning("spotty: unknown stylesheet placeholder %s", qPrintable(match.captured()));

    return sheet;
}

} // namespace

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    if (!g_instance)
        g_instance = this;
}

ThemeManager *ThemeManager::instance()
{
    return g_instance;
}

void ThemeManager::setTheme(Theme theme)
{
    m_theme = theme;
    apply();
    Q_EMIT themeChanged(m_theme);
}

void ThemeManager::apply()
{
    m_colors = m_theme == Theme::Dark ? darkColors() : lightColors();

    // Проверка на QApplication, а не QCoreApplication: класс должен оставаться безвредным
    // в консольном окружении, например в модульных тестах ядра.
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(buildPalette(m_colors));

        QFile file(QStringLiteral(":/themes/spotty.qss"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            app->setStyleSheet(resolveStylesheet(QString::fromUtf8(file.readAll()), m_colors));
    }
}

QString ThemeManager::themeToString(Theme theme)
{
    return theme == Theme::Light ? QStringLiteral("light") : QStringLiteral("dark");
}

ThemeManager::Theme ThemeManager::themeFromString(const QString &name, Theme fallback)
{
    if (name.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0)
        return Theme::Dark;
    if (name.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0)
        return Theme::Light;
    return fallback;
}

} // namespace spotty
