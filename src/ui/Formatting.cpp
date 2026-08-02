/**
 * \file Formatting.cpp
 * \brief Реализация spotty::Formatting.
 */
#include "Formatting.h"

#include <QDateTime>

namespace spotty {

QString Formatting::timeAgo(const QDateTime &when)
{
    if (!when.isValid())
        return {};

    const qint64 seconds = when.secsTo(QDateTime::currentDateTime());
    if (seconds < 10)
        return tr("just now");
    if (seconds < 60)
        return tr("%n s ago", nullptr, int(seconds));
    if (seconds < 3600)
        return tr("%n min ago", nullptr, int(seconds / 60));
    if (seconds < 86400)
        return tr("%n h ago", nullptr, int(seconds / 3600));
    return tr("%n d ago", nullptr, int(seconds / 86400));
}

QString Formatting::byteCount(qint64 bytes)
{
    // QT_TR_NOOP помечает строки для извлечения lupdate, а перевод берётся ниже, уже по
    // выбранному индексу: переводить нужно ровно одну приставку из четырёх.
    static const char *const units[] = {
        QT_TR_NOOP("B"), QT_TR_NOOP("KiB"), QT_TR_NOOP("MiB"), QT_TR_NOOP("GiB")};

    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }

    // Байты — целым числом: «1536 B» читается, «1536.0 B» выглядит нелепо.
    const QString number = unit == 0 ? QString::number(qint64(value))
                                     : QString::number(value, 'f', 1);
    return QStringLiteral("%1 %2").arg(number, tr(units[unit]));
}

} // namespace spotty
