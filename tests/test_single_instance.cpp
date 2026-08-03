/**
 * \file test_single_instance.cpp
 * \brief Тесты spotty::SingleInstanceGuard.
 */
#include "support/TestSupport.h"

#include <SingleInstanceGuard.h>

#include <QUuid>

#include <gtest/gtest.h>

using namespace spotty;
using spotty::test::waitFor;

namespace {

/**
 * \brief Уникальное имя на каждый тест.
 *
 * Иначе тесты цеплялись бы друг за друга через одно и то же имя сокета, а упавший
 * оставлял бы его занятым для следующих.
 */
QString uniqueKey()
{
    return QStringLiteral("spotty-test-%1")
        .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
}

} // namespace

TEST(SingleInstanceGuard, FirstAcquireSucceeds)
{
    SingleInstanceGuard guard(uniqueKey());

    EXPECT_TRUE(guard.tryAcquire());
}

TEST(SingleInstanceGuard, RepeatedAcquireBySameGuardSucceeds)
{
    SingleInstanceGuard guard(uniqueKey());

    ASSERT_TRUE(guard.tryAcquire());
    // Повторный вызов не должен ни падать, ни отбирать роль у самого себя.
    EXPECT_TRUE(guard.tryAcquire());
}

TEST(SingleInstanceGuard, SecondGuardIsRejected)
{
    const QString key = uniqueKey();

    SingleInstanceGuard first(key);
    ASSERT_TRUE(first.tryAcquire());

    SingleInstanceGuard second(key);

    // Это и есть смысл класса: второй экземпляр приложения не должен запуститься.
    EXPECT_FALSE(second.tryAcquire());
}

TEST(SingleInstanceGuard, DifferentKeysDoNotCollide)
{
    SingleInstanceGuard first(uniqueKey());
    SingleInstanceGuard second(uniqueKey());

    EXPECT_TRUE(first.tryAcquire());
    EXPECT_TRUE(second.tryAcquire());
}

TEST(SingleInstanceGuard, NotifyRaisesTheRunningInstance)
{
    const QString key = uniqueKey();

    SingleInstanceGuard first(key);
    ASSERT_TRUE(first.tryAcquire());

    bool raised = false;
    QObject::connect(&first, &SingleInstanceGuard::raiseRequested, [&] { raised = true; });

    SingleInstanceGuard second(key);
    ASSERT_FALSE(second.tryAcquire());
    EXPECT_TRUE(second.notifyExisting());

    // Ради этого сигнала второй экземпляр и запускается: он показывает окно первого.
    EXPECT_TRUE(waitFor([&] { return raised; }));
}

TEST(SingleInstanceGuard, NotifyWithoutRunningInstanceFails)
{
    SingleInstanceGuard guard(uniqueKey());

    // Никого нет — сообщать некому, и это не ошибка, а обычный случай первого запуска.
    EXPECT_FALSE(guard.notifyExisting());
}

TEST(SingleInstanceGuard, ReleasingAllowsAnotherToAcquire)
{
    const QString key = uniqueKey();

    {
        SingleInstanceGuard first(key);
        ASSERT_TRUE(first.tryAcquire());
    }

    // После завершения первого экземпляра имя должно освободиться, иначе программу
    // нельзя было бы запустить снова.
    SingleInstanceGuard second(key);
    EXPECT_TRUE(second.tryAcquire());
}
