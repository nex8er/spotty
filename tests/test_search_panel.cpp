/**
 * \file test_search_panel.cpp
 * \brief Проверка навигации с клавиатуры в панели поиска.
 */
#include "SearchPanel.h"

#include "support/FakePanelHost.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>

using namespace spotty;

TEST(SearchPanel, EnterMovesForwardAndShiftEnterMovesBackward)
{
    test::TempDir dir;
    test::FakePanelHost host(dir.path());
    SearchPanel panel(&host);
    auto *pattern = panel.findChild<QLineEdit *>();
    ASSERT_NE(pattern, nullptr);

    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(pattern, &enter);
    EXPECT_EQ(host.findNextCalls, 1);
    EXPECT_EQ(host.findPreviousCalls, 0);

    QKeyEvent shiftEnter(QEvent::KeyPress, Qt::Key_Return, Qt::ShiftModifier);
    QApplication::sendEvent(pattern, &shiftEnter);
    EXPECT_EQ(host.findNextCalls, 1);
    EXPECT_EQ(host.findPreviousCalls, 1);
}
