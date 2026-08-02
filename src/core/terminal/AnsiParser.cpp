/**
 * \file AnsiParser.cpp
 * \brief Реализация spotty::AnsiParser.
 */
#include "AnsiParser.h"

#include <QStringList>

namespace spotty {

namespace {

constexpr char16_t kEscape = 0x1B;
constexpr char16_t kBell = 0x07;
constexpr char16_t kBackspace = 0x08;
constexpr char16_t kTab = 0x09;
constexpr char16_t kLineFeed = 0x0A;
constexpr char16_t kCarriageReturn = 0x0D;

/// \brief Первый параметр последовательности либо \p fallback, если параметров нет.
int firstParameter(const QString &params, int fallback)
{
    if (params.isEmpty())
        return fallback;
    bool ok = false;
    const int value = QStringView(params).left(params.indexOf(u';')).toInt(&ok);
    return ok ? value : fallback;
}

} // namespace

AnsiParser::AnsiParser()
    : m_decoder(QStringConverter::Utf8)
{
}

void AnsiParser::setEncoding(QStringConverter::Encoding encoding)
{
    m_encoding = encoding;
    m_decoder = QStringDecoder(encoding);
}

void AnsiParser::reset()
{
    m_decoder = QStringDecoder(m_encoding);
    m_state = State::Ground;
    m_params.clear();
    m_pendingText.clear();
    m_style = TextStyle{};
}

void AnsiParser::parse(const QByteArray &data, Sink &sink)
{
    // Декодируем всю порцию сразу. Это безопасно: ESC и все байты последовательностей
    // лежат в ASCII и переживают декодирование без изменений, а QStringDecoder хранит
    // состояние и сам достроит символ, разорванный между порциями.
    const QString chunk = m_decoder.decode(data);

    for (const QChar ch : chunk) {
        const char16_t code = ch.unicode();

        switch (m_state) {
        case State::Ground:
            switch (code) {
            case kEscape:
                flushText(sink);
                m_state = State::Escape;
                break;
            case kLineFeed:
                flushText(sink);
                sink.lineFeed();
                break;
            case kCarriageReturn:
                flushText(sink);
                sink.carriageReturn();
                break;
            case kBackspace:
                flushText(sink);
                sink.backspace();
                break;
            case kTab:
                flushText(sink);
                sink.tab();
                break;
            default:
                // Прочие управляющие символы отбрасываем: показанные как есть, они
                // превратились бы в мусор посреди вывода.
                if (code >= 0x20 || code == 0x00)
                    m_pendingText.append(ch);
                break;
            }
            break;

        case State::Escape:
            if (code == u'[') {
                m_params.clear();
                m_state = State::Csi;
            } else if (code == u']') {
                m_state = State::Osc;
            } else {
                // Двухсимвольные последовательности (ESC c, ESC 7 и подобные) нам не
                // нужны — молча возвращаемся к тексту.
                m_state = State::Ground;
            }
            break;

        case State::Csi:
            if (code >= 0x40 && code <= 0x7E) {
                handleCsi(ch, sink);
                m_state = State::Ground;
            } else {
                m_params.append(ch);
                // Защита от бесконечного накопления, если в потоке встретился одинокий
                // ESC[ посреди двоичных данных.
                if (m_params.size() > 64) {
                    m_params.clear();
                    m_state = State::Ground;
                }
            }
            break;

        case State::Osc:
            // Строка операционной системы (заголовок окна и подобное). Содержимое нам
            // не нужно, важно лишь не выпустить его в текст.
            if (code == kBell)
                m_state = State::Ground;
            else if (code == kEscape)
                m_state = State::OscEscape;
            break;

        case State::OscEscape:
            // Завершитель ST — это ESC \. Любой другой символ означает, что ESC начинал
            // новую последовательность внутри OSC; такой поток уже испорчен, поэтому
            // просто возвращаемся к тексту.
            m_state = State::Ground;
            break;
        }
    }

    flushText(sink);
}

void AnsiParser::flushText(Sink &sink)
{
    if (m_pendingText.isEmpty())
        return;
    sink.text(m_pendingText, m_style);
    m_pendingText.clear();
}

void AnsiParser::handleCsi(QChar finalByte, Sink &sink)
{
    switch (finalByte.unicode()) {
    case u'm':
        applySgr();
        break;
    case u'K':
        sink.eraseInLine(firstParameter(m_params, 0));
        break;
    case u'J':
        sink.eraseInDisplay(firstParameter(m_params, 0));
        break;
    default:
        // Адресация курсора, прокрутка, запросы состояния. Распознаны и отброшены.
        break;
    }
    m_params.clear();
}

void AnsiParser::applySgr()
{
    // Приватные последовательности (ESC[?…m) к оформлению не относятся.
    if (m_params.startsWith(u'?') || m_params.startsWith(u'>'))
        return;

    // Пустые параметры равнозначны нулю: «ESC[m» — это сброс.
    const QStringList parts = m_params.isEmpty() ? QStringList{QStringLiteral("0")}
                                                 : m_params.split(u';');

    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        // Пустой параметр в середине списка тоже означает ноль: «ESC[1;;31m».
        const int code = parts.at(i).isEmpty() ? 0 : parts.at(i).toInt(&ok);
        if (!parts.at(i).isEmpty() && !ok)
            continue;

        switch (code) {
        case 0:
            m_style = TextStyle{};
            break;
        case 1: m_style.bold = true; break;
        case 2: m_style.faint = true; break;
        case 3: m_style.italic = true; break;
        case 4: m_style.underline = true; break;
        case 7: m_style.inverse = true; break;
        case 9: m_style.strikeOut = true; break;

        case 21: // Двойное подчёркивание в одних терминалах, снятие жирного в других.
        case 22:
            m_style.bold = false;
            m_style.faint = false;
            break;
        case 23: m_style.italic = false; break;
        case 24: m_style.underline = false; break;
        case 27: m_style.inverse = false; break;
        case 29: m_style.strikeOut = false; break;

        case 39:
            m_style.foregroundSource = ColorSource::Default;
            break;
        case 49:
            m_style.backgroundSource = ColorSource::Default;
            break;

        case 38:
        case 48: {
            // Расширенный цвет: 38;5;n — номер в палитре, 38;2;r;g;b — точный цвет.
            const bool foreground = code == 38;
            if (i + 1 >= parts.size())
                break;

            const int kind = parts.at(i + 1).toInt();
            if (kind == 5 && i + 2 < parts.size()) {
                const quint8 index = quint8(qBound(0, parts.at(i + 2).toInt(), 255));
                if (foreground) {
                    m_style.foregroundSource = ColorSource::Indexed;
                    m_style.foregroundIndex = index;
                } else {
                    m_style.backgroundSource = ColorSource::Indexed;
                    m_style.backgroundIndex = index;
                }
                i += 2;
            } else if (kind == 2 && i + 4 < parts.size()) {
                // Упаковка 0x00RRGGBB вручную: qRgb() живёт в QtGui, а ядро от него не
                // зависит.
                const quint32 rgb = (quint32(qBound(0, parts.at(i + 2).toInt(), 255)) << 16)
                                  | (quint32(qBound(0, parts.at(i + 3).toInt(), 255)) << 8)
                                  | quint32(qBound(0, parts.at(i + 4).toInt(), 255));
                if (foreground) {
                    m_style.foregroundSource = ColorSource::Rgb;
                    m_style.foregroundRgb = rgb;
                } else {
                    m_style.backgroundSource = ColorSource::Rgb;
                    m_style.backgroundRgb = rgb;
                }
                i += 4;
            }
            break;
        }

        default:
            if (code >= 30 && code <= 37) {
                m_style.foregroundSource = ColorSource::Indexed;
                m_style.foregroundIndex = quint8(code - 30);
            } else if (code >= 40 && code <= 47) {
                m_style.backgroundSource = ColorSource::Indexed;
                m_style.backgroundIndex = quint8(code - 40);
            } else if (code >= 90 && code <= 97) {
                // Яркие цвета — это номера 8..15 той же палитры.
                m_style.foregroundSource = ColorSource::Indexed;
                m_style.foregroundIndex = quint8(code - 90 + 8);
            } else if (code >= 100 && code <= 107) {
                m_style.backgroundSource = ColorSource::Indexed;
                m_style.backgroundIndex = quint8(code - 100 + 8);
            }
            break;
        }
    }
}

} // namespace spotty
