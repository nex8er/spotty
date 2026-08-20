/**
 * \file CsvDetector.cpp
 * \brief Реализация spotty::CsvDetector.
 */
#include <spotty/data/CsvDetector.h>

#include <QRegularExpression>

namespace spotty {

namespace {

/// \brief Наименьшее число полей, при котором строка считается данными. См. заголовок.
constexpr qsizetype kMinimumFields = 2;

/**
 * \brief Проверить, что начало последнего поля ещё может стать числом.
 * \param field Поле после последнего разделителя; может быть неполным.
 * \return `true`, если к нему достаточно дописать цифры или показатель степени.
 *
 * Готовое число отдаём QString::toDouble(), чтобы правило совпадало с isDataLine().
 * Регулярное выражение остаётся только для оборванных `-`, `1.`, `1e` и `1e+`.
 */
bool isNumberPrefix(const QString &field)
{
    const QString trimmed = field.trimmed();
    if (trimmed.isEmpty())
        return true;

    bool numberOk = false;
    trimmed.toDouble(&numberOk);
    if (numberOk)
        return true;

    static const QRegularExpression kIncompleteNumber(
        QStringLiteral("^[+-]?(?:(?:\\d+(?:\\.\\d*)?)|(?:\\.\\d*))?(?:[eE][+-]?\\d*)?$"));
    return kIncompleteNumber.match(trimmed).hasMatch();
}

} // namespace

bool CsvDetector::isDataLine(const QString &text) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    // KeepEmptyParts намеренно: «1,,3» — это испорченные данные, а не сообщение, и
    // пустое поле должно отменить распознавание, а не молча пропасть.
    const QStringList fields = trimmed.split(m_separator, Qt::KeepEmptyParts);
    if (fields.size() < kMinimumFields)
        return false;

    for (const QString &field : fields) {
        bool ok = false;
        field.trimmed().toDouble(&ok);
        if (!ok)
            return false;
    }
    return true;
}

bool CsvDetector::isPotentialDataLine(const QString &text) const
{
    // Пробелы вокруг значений разрешены и у законченной строки, поэтому отделять по ним
    // сообщение от телеметрии нельзя: «1, 2» остаётся возможным CSV до конца строки.
    const QStringList fields = text.split(m_separator, Qt::KeepEmptyParts);
    for (qsizetype index = 0; index + 1 < fields.size(); ++index) {
        bool numberOk = false;
        fields.at(index).trimmed().toDouble(&numberOk);
        if (!numberOk)
            return false;
    }

    return isNumberPrefix(fields.last());
}

} // namespace spotty
