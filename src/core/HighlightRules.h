/**
 * \file HighlightRules.h
 * \brief Правила подсветки строк по регулярным выражениям.
 */
#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QVariantList>

namespace spotty {

/**
 * \struct HighlightRule
 * \brief Одно правило подсветки.
 */
struct HighlightRule
{
    QString name;    ///< Подпись в списке правил.
    QString pattern; ///< Регулярное выражение.
    bool caseSensitive = false;
    bool enabled = true;

    /// \brief Цвет подсветки, упакованный как `0x00RRGGBB`.
    quint32 color = 0xD26B6B;
};

/**
 * \class HighlightRules
 * \brief Набор правил подсветки с кэшем скомпилированных выражений.
 *
 * \par Зачем кэш
 *
 * Правила проверяются на каждой отрисовке для каждой видимой строки. Компиляция
 * регулярного выражения на каждую проверку сделала бы прокрутку заметно медленной, поэтому
 * выражения компилируются один раз при установке правил.
 *
 * \par Порядок правил
 *
 * Побеждает первое подошедшее включённое правило. Порядок задаёт пользователь: так
 * частное правило можно поставить выше общего и не выдумывать приоритеты.
 */
class HighlightRules
{
public:
    void setRules(const QList<HighlightRule> &rules);
    const QList<HighlightRule> &rules() const { return m_rules; }

    /// \return `true`, если нет ни одного включённого правила — тогда проверку можно
    ///         пропустить целиком.
    bool isEmpty() const { return m_compiled.isEmpty(); }

    /**
     * \brief Найти правило, подходящее к строке.
     * \param text Текст строки.
     * \return Индекс правила в rules() или -1.
     */
    int match(const QString &text) const;

    /// \brief Цвет правила по индексу; чёрный при неверном индексе.
    quint32 colorAt(int index) const;

    /// \brief Представление для записи в настройки.
    QVariantList toVariant() const;

    /// \brief Восстановление из настроек.
    void fromVariant(const QVariantList &list);

private:
    /// \brief Скомпилированное выражение вместе с индексом исходного правила.
    struct Compiled
    {
        QRegularExpression regex;
        int ruleIndex = -1;
    };

    /// \brief Перекомпилировать включённые правила.
    void compile();

    QList<HighlightRule> m_rules;
    QList<Compiled> m_compiled;
};

} // namespace spotty
