/**
 * \file test_plot_format.cpp
 * \brief Тесты подписей плоттера: имена рядов и числа под заданную ширину.
 */
#include <spotty/data/PlotFormat.h>

#include <gtest/gtest.h>

#include <QLocale>

#include <cmath>

using namespace spotty;

TEST(PlotFormat, DefaultNamesFollowTheNatoAlphabet)
{
    EXPECT_EQ(PlotFormat::defaultSeriesName(0), QStringLiteral("alpha"));
    EXPECT_EQ(PlotFormat::defaultSeriesName(1), QStringLiteral("bravo"));
    EXPECT_EQ(PlotFormat::defaultSeriesName(2), QStringLiteral("charlie"));
    EXPECT_EQ(PlotFormat::defaultSeriesName(25), QStringLiteral("zulu"));
}

TEST(PlotFormat, EveryNameInTheAlphabetIsDistinct)
{
    // Весь смысл алфавита в различимости: повтор или опечатка, дающая два одинаковых имени,
    // молча слепила бы два ряда в таблице.
    QSet<QString> seen;
    for (int i = 0; i < PlotFormat::kNamedSeriesCount; ++i)
        seen.insert(PlotFormat::defaultSeriesName(i));

    EXPECT_EQ(seen.size(), PlotFormat::kNamedSeriesCount);
}

TEST(PlotFormat, NamesPastZuluAreColumnNumbers)
{
    // Считая с единицы: пользователь считает колонки с первой, а не с нулевой.
    EXPECT_EQ(PlotFormat::defaultSeriesName(26), QStringLiteral("27"));
    EXPECT_EQ(PlotFormat::defaultSeriesName(63), QStringLiteral("64"));
}

TEST(PlotFormat, NegativeIndexIsVisiblyWrongRatherThanSilentlyZulu)
{
    // Отрицательный номер — ошибка вызывающего. Отдать за него настоящее имя значило бы
    // спрятать ошибку в правдоподобном выводе.
    EXPECT_EQ(PlotFormat::defaultSeriesName(-1), QStringLiteral("-1"));
}

TEST(PlotFormat, DigitsGrowWithTheAvailableWidth)
{
    EXPECT_LT(PlotFormat::digitsForCharacters(8), PlotFormat::digitsForCharacters(11));
    EXPECT_EQ(PlotFormat::digitsForCharacters(8), 5);
}

TEST(PlotFormat, DigitsAreClampedAtBothEnds)
{
    // Узкая панель не должна доводить точность до нуля, широкая — до шума округления.
    EXPECT_EQ(PlotFormat::digitsForCharacters(0), 3);
    EXPECT_EQ(PlotFormat::digitsForCharacters(4), 3);
    EXPECT_EQ(PlotFormat::digitsForCharacters(200), 9);
}

TEST(PlotFormat, DigitCountIsStableWhileValuesChange)
{
    // Требование владельца: цифры в таблице не должны прыгать на живом потоке. Число знаков
    // зависит только от ширины поля, поэтому меняется ровно тогда, когда панель тянут за
    // край, — и никогда от того, что пришло очередное значение.
    const int digits = PlotFormat::digitsForCharacters(9);

    const QString small = PlotFormat::number(1.0, digits);
    const QString large = PlotFormat::number(999999.0, digits);
    const QString negative = PlotFormat::number(-0.00012345, digits);

    EXPECT_FALSE(small.isEmpty());
    EXPECT_FALSE(large.isEmpty());
    EXPECT_FALSE(negative.isEmpty());
    EXPECT_EQ(PlotFormat::digitsForCharacters(9), digits);
}

TEST(PlotFormat, NumberKeepsTheRequestedSignificantDigits)
{
    EXPECT_EQ(PlotFormat::number(123.456789, 5), QStringLiteral("123.46"));
    EXPECT_EQ(PlotFormat::number(-123.456789, 5), QStringLiteral("-123.46"));
}

TEST(PlotFormat, FittedKeepsWhatFits)
{
    // Восемь знакомест — пять значащих цифр: «123.46» укладывается.
    EXPECT_EQ(PlotFormat::fitted(123.456789, 8), QStringLiteral("123.46"));
}

TEST(PlotFormat, FittedReplacesOverflowWithAnEllipsis)
{
    // Обрезок вместо многоточия был бы другим числом: «-1.23…» от -1.2346e-05 отличается
    // на пять порядков, и прочитавший его не заподозрит подвоха.
    const QString text = PlotFormat::fitted(-0.0000123456, 6);

    EXPECT_EQ(text, QStringLiteral("…"));
}

TEST(PlotFormat, FittedNeverTruncatesTheNumber)
{
    // Что бы ни вышло, это либо полное число, либо многоточие — но не начало числа.
    for (const double value : {1.0, -1.0, 1e-9, 1e9, -123456.789, 0.5}) {
        for (int characters = 4; characters <= 14; ++characters) {
            const QString text = PlotFormat::fitted(value, characters);
            if (text == QStringLiteral("…"))
                continue;
            bool ok = false;
            text.toDouble(&ok);
            EXPECT_TRUE(ok) << "«" << qPrintable(text) << "» при ширине " << characters;
            EXPECT_LE(text.size(), characters);
        }
    }
}

TEST(PlotFormat, FittedDoesNotShrinkPrecisionToSqueezeIn)
{
    // Ужимать точность ради того, чтобы влезло, значило бы показывать соседние строки с
    // разной точностью. Ширина одна — значит, и знаков поровну.
    const QString small = PlotFormat::fitted(1.23456789, 9);
    const QString large = PlotFormat::fitted(9.87654321, 9);

    EXPECT_EQ(small.size(), large.size());
}

TEST(PlotFormat, MinimumWidthStillHoldsFiveDigits)
{
    // Нижний предел ширины статистики — восемь знакомест, и они обязаны нести пять
    // значащих цифр даже со знаком минуса: минус в счёт цифр не идёт.
    EXPECT_EQ(PlotFormat::digitsForCharacters(8), 5);
    EXPECT_EQ(PlotFormat::fitted(-12345.0, 8), QStringLiteral("-12345"));
}

TEST(PlotFormat, MeanLimitsTheFractionalPart)
{
    // Среднее целых 1, 2 и 2 равно 1.6666667 — шесть знаков после запятой тут не точность,
    // а шум: исходные данные её не несли.
    EXPECT_EQ(PlotFormat::fittedMean(5.0 / 3.0, 10, 3), QStringLiteral("1.667"));
    EXPECT_EQ(PlotFormat::fittedMean(123.456789, 10, 3), QStringLiteral("123.457"));
}

TEST(PlotFormat, MeanDropsTrailingZeros)
{
    // «5.000» занимает место, которого стоила бы лишняя цифра у соседа, и ничего не
    // сообщает.
    EXPECT_EQ(PlotFormat::fittedMean(5.0, 10, 3), QStringLiteral("5"));
    EXPECT_EQ(PlotFormat::fittedMean(2.5, 10, 3), QStringLiteral("2.5"));
}

TEST(PlotFormat, MeanKeepsSmallValuesIntact)
{
    // Ниже единицы дробная часть — это всё значение целиком, и обрезать её нечем: с тремя
    // знаками после запятой от 0.000123 не осталось бы ничего.
    const QString text = PlotFormat::fittedMean(0.000123456, 12, 3);

    EXPECT_NE(text, QStringLiteral("0"));
    bool ok = false;
    EXPECT_NEAR(text.toDouble(&ok), 0.000123456, 1e-8);
    EXPECT_TRUE(ok);
}

TEST(PlotFormat, MeanFallsBackToAnEllipsisWhenTooWide)
{
    EXPECT_EQ(PlotFormat::fittedMean(-123456789.5, 5, 3), QStringLiteral("…"));
}

TEST(PlotFormat, FittedShowsNonFiniteAsDash)
{
    EXPECT_EQ(PlotFormat::fitted(std::nan(""), 8), QStringLiteral("—"));
}

TEST(PlotFormat, NonFiniteRendersAsDash)
{
    // Пустая ячейка читалась бы как «ещё не посчитано», а «nan» — как неисправность, тогда
    // как пропущенный отсчёт в телеметрии дело обычное.
    EXPECT_EQ(PlotFormat::number(std::nan(""), 5), QStringLiteral("—"));
    EXPECT_EQ(PlotFormat::number(std::numeric_limits<double>::infinity(), 5),
              QStringLiteral("—"));
}

TEST(PlotFormat, NumberIsLocaleIndependent)
{
    // Число уходит в подпись графика и в экспорт CSV рядом с машинными данными. Десятичная
    // запятая немецкой локали сделала бы «1,5» неотличимым от двух полей.
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));

    EXPECT_EQ(PlotFormat::number(1.5, 5), QStringLiteral("1.5"));

    QLocale::setDefault(previous);
}
