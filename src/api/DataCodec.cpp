/**
 * \file DataCodec.cpp
 * \brief Реализация spotty::DataCodec.
 */
#include <spotty/data/DataCodec.h>

namespace spotty {

QByteArray DataCodec::terminationBytes(Termination termination)
{
    switch (termination) {
    case Termination::Lf:
        return QByteArrayLiteral("\n");
    case Termination::Cr:
        return QByteArrayLiteral("\r");
    case Termination::CrLf:
        return QByteArrayLiteral("\r\n");
    case Termination::Nul:
        return QByteArray(1, '\0');
    case Termination::None:
        break;
    }
    return {};
}

QByteArray DataCodec::encode(const QString &input, Format format, Termination termination,
                             QString *error)
{
    if (error)
        error->clear();

    QByteArray payload;

    switch (format) {
    case Format::Text:
        payload = input.toUtf8();
        break;

    case Format::Hex:
        payload = fromHex(input, error);
        if (error && !error->isEmpty())
            return {};
        break;

    case Format::Base64: {
        // Строгий разбор: тихо проглоченный мусор превратился бы в мусор, отправленный
        // устройству, а причину было бы не найти.
        const auto decoded = QByteArray::fromBase64Encoding(input.toUtf8(),
                                                            QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            if (error)
                *error = tr("Not valid Base64.");
            return {};
        }
        payload = *decoded;
        break;
    }
    }

    return payload + terminationBytes(termination);
}

QByteArray DataCodec::fromHex(const QString &input, QString *error)
{
    if (error)
        error->clear();

    // Разделители убираем, потому что байты копируют откуда угодно: из даташита через
    // пробел, из лога анализатора через двоеточие, из чужого кода через «0x…, 0x…».
    QString cleaned;
    cleaned.reserve(input.size());
    for (int i = 0; i < input.size(); ++i) {
        const QChar ch = input.at(i);
        if (ch.isSpace() || ch == u',' || ch == u':' || ch == u'-' || ch == u';')
            continue;
        // Приставка 0x и 0X пропускается целиком.
        if (ch == u'0' && i + 1 < input.size()
            && (input.at(i + 1) == u'x' || input.at(i + 1) == u'X')) {
            ++i;
            continue;
        }
        if (!isxdigit(ch.toLatin1())) {
            if (error)
                *error = tr("\"%1\" is not a hexadecimal digit.").arg(ch);
            return {};
        }
        cleaned.append(ch);
    }

    if (cleaned.isEmpty())
        return {};

    if (cleaned.size() % 2 != 0) {
        if (error)
            *error = tr("Odd number of hexadecimal digits - one byte is two digits.");
        return {};
    }

    return QByteArray::fromHex(cleaned.toLatin1());
}

QString DataCodec::toHex(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

QString DataCodec::formatName(Format format)
{
    switch (format) {
    case Format::Hex:
        return tr("Hex");
    case Format::Base64:
        return tr("Base64");
    case Format::Text:
        break;
    }
    return tr("Text");
}

QString DataCodec::terminationName(Termination termination)
{
    switch (termination) {
    case Termination::Lf:
        return QStringLiteral("LF");
    case Termination::Cr:
        return QStringLiteral("CR");
    case Termination::CrLf:
        return QStringLiteral("CR+LF");
    case Termination::Nul:
        return QStringLiteral("NUL");
    case Termination::None:
        break;
    }
    return tr("None");
}

} // namespace spotty
