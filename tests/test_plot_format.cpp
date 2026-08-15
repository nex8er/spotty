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
