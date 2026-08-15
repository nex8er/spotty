/**
 * \file PlotFormat.cpp
 * \brief Реализация spotty::PlotFormat.
 */
#include <spotty/data/PlotFormat.h>

#include <QLatin1StringView>
#include <QtGlobal>
#include <QtMath>

namespace spotty {

namespace {

/// \brief Алфавит ИКАО в написании самой организации: `juliett` и `xray` именно так.
constexpr const char *kNatoAlphabet[PlotFormat::kNamedSeriesCount] = {
    "alpha",   "bravo",  "charlie", "delta",  "echo",   "foxtrot", "golf",
    "hotel",   "india",  "juliett", "kilo",   "lima",   "mike",    "november",
    "oscar",   "papa",   "quebec",  "romeo",  "sierra", "tango",   "uniform",
    "victor",  "whiskey", "xray",   "yankee", "zulu",
};

/// \name Границы числа значащих цифр
/// Ниже трёх цифра перестаёт нести смысл (`1e+02` вместо `123`), выше девяти упирается в
/// точность double, и лишние знаки — уже шум округления, а не данные.
/// @{
constexpr int kMinimumDigits = 3;
constexpr int kMaximumDigits = 9;
/// @}

/// \brief Знакоместа под минус, десятичную точку и запас на округление.
constexpr int kReservedCharacters = 3;

} // namespace

QString PlotFormat::defaultSeriesName(int index)
{
    // Отрицательный номер сюда попасть не должен, но молча выдать «zulu» за колонку -1
    // хуже, чем выдать заведомо странное имя: последнее видно на экране.
    if (index < 0)
        return QString::number(index);

    if (index < kNamedSeriesCount)
        return QString::fromLatin1(kNatoAlphabet[index]);

    // Дальше — номер колонки, считая с единицы: пользователь считает колонки с первой, а
    // не с нулевой. Второй круг с цифрой («alpha 2») читался бы как порядковый номер
    // внутри альфы и спорил бы с первым кругом.
    return QString::number(index + 1);
}

int PlotFormat::digitsForCharacters(int characters)
{
    return qBound(kMinimumDigits, characters - kReservedCharacters, kMaximumDigits);
}

QString PlotFormat::number(double value, int digits)
{
    if (!qIsFinite(value))
        return QStringLiteral("—");

    return QString::number(value, 'g', qBound(kMinimumDigits, digits, kMaximumDigits));
}

} // namespace spotty
