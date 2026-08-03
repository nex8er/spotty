/**
 * \file main.cpp
 * \brief Точка входа набора тестов.
 */
#include <spotty/api/ChannelState.h>

#include <QCoreApplication>

#include <gtest/gtest.h>

/**
 * \brief Запускает все тесты.
 *
 * QCoreApplication создаётся до тестов, потому что от него зависит слишком многое в
 * проверяемом коде: tr() для сообщений об ошибках, таймеры, очередь событий, определение
 * путей. Без него половина тестов падала бы не по существу, а на отсутствии приложения.
 *
 * QApplication здесь намеренно не используется: `spotty-core` не знает про виджеты, и
 * тесты не должны требовать графической подсистемы — иначе их нельзя было бы гонять в
 * контейнере непрерывной интеграции.
 */
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("SpottyTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SpottyTests"));

    qRegisterMetaType<spotty::ChannelState>();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
