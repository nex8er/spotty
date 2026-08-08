/**
 * \file CsvDetector.cpp
 * \brief Реализация spotty::CsvDetector.
 */
#include <spotty/data/CsvDetector.h>

namespace spotty {

namespace {

/// \brief Наименьшее число полей, при котором строка считается данными. См. заголовок.
constexpr qsizetype kMinimumFields = 2;

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

} // namespace spotty
