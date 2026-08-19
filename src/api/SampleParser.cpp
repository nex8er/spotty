/**
 * \file SampleParser.cpp
 * \brief Реализация spotty::SampleParser.
 */
#include <spotty/data/SampleParser.h>

#include <QtGlobal>
#include <QtMath>

namespace spotty {

namespace {

/// \brief Наименьшее число полей, при котором строка может оказаться заголовком.
constexpr int kMinimumHeaderFields = 2;

/**
 * \brief Разбить по пробельному разделителю, схлопывая серии.
 *
 * Устройства выравнивают колонки пробелами — `1  23   4`, — и считать каждый пробел
 * отдельным разделителем значило бы получать разное число полей на каждой строке. Ручной
 * обход, а не регулярное выражение: строки приходят тысячами в секунду, и компиляция
 * совпадений на каждую из них здесь ни к чему.
 */
QStringList splitOnWhitespaceRuns(const QString &line)
{
    QStringList fields;
    int start = -1;

    for (int i = 0; i < line.size(); ++i) {
        const bool space = line.at(i) == u' ' || line.at(i) == u'\t';
        if (space) {
            if (start >= 0) {
                fields.append(line.mid(start, i - start));
                start = -1;
            }
        } else if (start < 0) {
            start = i;
        }
    }
    if (start >= 0)
        fields.append(line.mid(start));

    return fields;
}

} // namespace

bool SampleParser::separatorIsWhitespace() const
{
    return m_separator == u' ' || m_separator == u'\t';
}

SampleParser::Result SampleParser::parse(const QString &line, int currentColumnCount,
                                         QList<double> *values, QStringList *names) const
{
    Result result;

    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return result;

    const QStringList fields = separatorIsWhitespace()
                                   ? splitOnWhitespaceRuns(trimmed)
                                   : trimmed.split(m_separator, Qt::KeepEmptyParts);
    if (fields.isEmpty())
        return result;

    result.fieldCount = int(fields.size());

    QList<double> parsed;
    parsed.reserve(fields.size());

    int numeric = 0;
    int empty = 0;

    for (const QString &field : fields) {
        const QString text = field.trimmed();
        if (text.isEmpty()) {
            ++empty;
            parsed.append(qQNaN());
            continue;
        }

        bool ok = false;
        // Именно QString::toDouble, а не QLocale::toDouble: он разбирает в C-локали, на
        // которой и говорит устройство. С локалью пользователя «1.5» на немецкой системе
        // перестало бы разбираться, а «1,5» разобралось бы как одно поле вместо двух.
        const double value = text.toDouble(&ok);
        if (ok) {
            ++numeric;
            // Бесконечность и «nan» от устройства — это отсутствующее значение, а не число:
            // пропустив их дальше, мы получили бы шкалу от минус бесконечности до плюс.
            parsed.append(qIsFinite(value) ? value : qQNaN());
        } else {
            parsed.append(qQNaN());
        }
    }

    const int textual = result.fieldCount - numeric - empty;

    // Заголовок: сплошной текст без единого числа и без пропусков.
    if (textual == result.fieldCount && result.fieldCount >= kMinimumHeaderFields
        && (currentColumnCount == 0 || result.fieldCount == currentColumnCount)) {
        if (names) {
            names->clear();
            names->reserve(fields.size());
            for (const QString &field : fields)
                names->append(field.trimmed());
        }
        result.outcome = Outcome::Header;
        return result;
    }

    // Данные: хоть одно число и ни одного слова. Смесь вроде «temp,25.3» — это подписанное
    // значение, адресованное человеку, и попытка построить по нему график дала бы колонку
    // из одних пропусков.
    if (numeric > 0 && textual == 0) {
        if (values)
            *values = std::move(parsed);
        result.outcome = Outcome::Data;
        return result;
    }

    return result;
}

} // namespace spotty
