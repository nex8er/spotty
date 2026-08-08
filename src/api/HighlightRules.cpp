/**
 * \file HighlightRules.cpp
 * \brief Реализация spotty::HighlightRules.
 */
#include <spotty/data/HighlightRules.h>

#include <QVariantMap>

namespace spotty {

void HighlightRules::setRules(const QList<HighlightRule> &rules)
{
    m_rules = rules;
    compile();
}

void HighlightRules::compile()
{
    m_compiled.clear();

    for (int i = 0; i < m_rules.size(); ++i) {
        const HighlightRule &rule = m_rules.at(i);
        if (!rule.enabled || rule.pattern.isEmpty())
            continue;

        QRegularExpression regex(rule.pattern);
        if (!rule.caseSensitive)
            regex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);

        // Некорректное выражение просто не участвует в подсветке. Отвергать его при вводе
        // нельзя: пользователь набирает по одному символу, и половина набранного почти
        // всегда некорректна.
        if (!regex.isValid())
            continue;

        // Оптимизация окупается: выражение применяется к каждой видимой строке на каждой
        // отрисовке.
        regex.optimize();
        m_compiled.append(Compiled{regex, i});
    }
}

int HighlightRules::match(const QString &text) const
{
    for (const Compiled &compiled : m_compiled) {
        if (compiled.regex.match(text).hasMatch())
            return compiled.ruleIndex;
    }
    return -1;
}

quint32 HighlightRules::colorAt(int index) const
{
    if (index < 0 || index >= m_rules.size())
        return 0;
    return m_rules.at(index).color;
}

QVariantList HighlightRules::toVariant() const
{
    QVariantList list;
    for (const HighlightRule &rule : m_rules) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), rule.name);
        map.insert(QStringLiteral("pattern"), rule.pattern);
        map.insert(QStringLiteral("caseSensitive"), rule.caseSensitive);
        map.insert(QStringLiteral("enabled"), rule.enabled);
        map.insert(QStringLiteral("color"), qint64(rule.color));
        list.append(map);
    }
    return list;
}

void HighlightRules::fromVariant(const QVariantList &list)
{
    QList<HighlightRule> rules;
    for (const QVariant &value : list) {
        const QVariantMap map = value.toMap();
        HighlightRule rule;
        rule.name = map.value(QStringLiteral("name")).toString();
        rule.pattern = map.value(QStringLiteral("pattern")).toString();
        rule.caseSensitive = map.value(QStringLiteral("caseSensitive")).toBool();
        rule.enabled = map.value(QStringLiteral("enabled"), true).toBool();
        rule.color = quint32(map.value(QStringLiteral("color"), 0xD26B6B).toLongLong());
        rules.append(rule);
    }
    setRules(rules);
}

} // namespace spotty
