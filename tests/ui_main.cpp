/**
 * \file ui_main.cpp
 * \brief Точка входа набора тестов панельного SDK.
 */
#include <spotty/api/ChannelState.h>

#include <QApplication>

#include <gtest/gtest.h>

/**
 * \brief Запускает тесты, которым нужны настоящие виджеты.
 *
 * Отдельная точка входа, а не общая с `spotty-tests`: там намеренно создаётся
 * QCoreApplication, потому что ядро не знает про виджеты и его тесты не должны требовать
 * графической подсистемы. Здесь всё наоборот — проверяется как раз слой виджетов, а
 * QWidget без QApplication не создаётся вовсе.
 *
 * Графическая подсистема при этом не нужна: набор запускается с
 * `QT_QPA_PLATFORM=offscreen`, так что он работает и в контейнере непрерывной интеграции.
 *
 * \note Понадобилось это не сразу. Пока UI-набор пользовался общей точкой входа, ни один
 *       случай не мог создать виджет, и вся проводка панелей — что на что подписано, доходит
 *       ли щелчок до делегата — оставалась непроверяемой. Первая же ошибка этого рода
 *       (галочка видимости меняла модель, но не сама себя) дошла до пользователя.
 */
int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("SpottyTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SpottyTests"));

    qRegisterMetaType<spotty::ChannelState>();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
