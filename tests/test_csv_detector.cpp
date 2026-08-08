/**
 * \file test_csv_detector.cpp
 * \brief Тесты spotty::CsvDetector.
 */
#include <spotty/data/CsvDetector.h>

#include <gtest/gtest.h>

using namespace spotty;

TEST(CsvDetector, RecognisesNumericLine)
{
    CsvDetector detector;
    EXPECT_TRUE(detector.isDataLine(QStringLiteral("12.5,3,-7")));
    EXPECT_TRUE(detector.isDataLine(QStringLiteral("1,2")));
}

TEST(CsvDetector, IgnoresMessages)
{
    CsvDetector detector;
    EXPECT_FALSE(detector.isDataLine(QStringLiteral("sensor init ok")));
    // Подписанное значение адресовано человеку не меньше, чем машине.
    EXPECT_FALSE(detector.isDataLine(QStringLiteral("temp,25.3")));
}

TEST(CsvDetector, SingleFieldIsNotData)
{
    CsvDetector detector;
    // Ответ «42» на команду опроса — такое же сообщение, как «ok», и прятать его нельзя.
    EXPECT_FALSE(detector.isDataLine(QStringLiteral("42")));
}

TEST(CsvDetector, EmptyFieldCancelsRecognition)
{
    CsvDetector detector;
    // «1,,3» — испорченные данные, а не сообщение; молча проглотить их нельзя.
    EXPECT_FALSE(detector.isDataLine(QStringLiteral("1,,3")));
}

TEST(CsvDetector, AllowsSpacesAroundFields)
{
    CsvDetector detector;
    // Устройства выравнивают колонки пробелами сплошь и рядом; отличать данные от
    // сообщений по форматированию было бы произволом.
    EXPECT_TRUE(detector.isDataLine(QStringLiteral(" 1, 2 , 3 ")));
}

TEST(CsvDetector, EmptyLineIsNotData)
{
    CsvDetector detector;
    EXPECT_FALSE(detector.isDataLine(QString()));
    EXPECT_FALSE(detector.isDataLine(QStringLiteral("   ")));
}

TEST(CsvDetector, SeparatorIsConfigurable)
{
    CsvDetector detector;
    detector.setSeparator(u';');
    EXPECT_TRUE(detector.isDataLine(QStringLiteral("1;2;3")));
    EXPECT_FALSE(detector.isDataLine(QStringLiteral("1,2,3")));
}

TEST(CsvDetector, ScientificNotationCounts)
{
    CsvDetector detector;
    EXPECT_TRUE(detector.isDataLine(QStringLiteral("1.5e-3,2E4")));
}
