/**
 * \file test_series_swatch.cpp
 * \brief Тесты первой колонки таблицы рядов: доходит ли щелчок до делегата.
 */
#include "SeriesSwatchDelegate.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QMouseEvent>
#include <QTableWidget>

#include <functional>

using namespace spotty;

namespace {

/**
 * \brief Таблица с делегатом и одной строкой.
 *
 * Проверяется именно связка «вид — делегат»: щелчок обязан дойти до editorEvent(). Этот
 * путь и выглядел сломанным — галочка не переключалась.
 *
 * События посылаются вручную, а сигналы считаются обычным connect(): и QTest, и QSignalSpy
 * живут в компоненте Qt6::Test, который пришлось бы добавить во все шесть сборок
 * непрерывной интеграции ради одного набора.
 */
class Swatch : public ::testing::Test
{
protected:
    void SetUp() override
    {
        table.setColumnCount(1);
        table.setRowCount(1);

        auto *item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setData(Qt::CheckStateRole, Qt::Checked);
        item->setData(SeriesSwatchDelegate::kColorRole, 0xFF6CB6FFu);
        table.setItem(0, 0, item);

        table.setItemDelegateForColumn(0, &delegate);
        table.resize(200, 100);
        table.show();
        QApplication::processEvents();
    }

    /// \brief Точка в середине единственной ячейки, в координатах области просмотра.
    QPoint cellCentre() const
    {
        return table.visualItemRect(table.item(0, 0)).center();
    }

    void send(QEvent::Type type, Qt::MouseButton button)
    {
        const QPointF point(cellCentre());
        QMouseEvent event(type, point, table.viewport()->mapToGlobal(point), button, button,
                          Qt::NoModifier);
        QApplication::sendEvent(table.viewport(), &event);
    }

    void click(Qt::MouseButton button = Qt::LeftButton)
    {
        send(QEvent::MouseButtonPress, button);
        send(QEvent::MouseButtonRelease, button);
    }

    /// \brief Считать, сколько раз пришёл сигнал делегата.
    template <typename Signal>
    int countOf(Signal signal, const std::function<void()> &action)
    {
        int seen = 0;
        const auto connection =
            QObject::connect(&delegate, signal, [&seen] { ++seen; });
        action();
        QObject::disconnect(connection);
        return seen;
    }

    QTableWidget table;
    SeriesSwatchDelegate delegate;
};

} // namespace

TEST_F(Swatch, SingleClickReachesTheDelegate)
{
    int row = -1;
    QObject::connect(&delegate, &SeriesSwatchDelegate::visibilityToggled,
                     [&row](int toggled) { row = toggled; });

    click();

    EXPECT_EQ(row, 0);
}

TEST_F(Swatch, DoubleClickAsksForTheColour)
{
    const int seen = countOf(&SeriesSwatchDelegate::colourRequested, [this] {
        send(QEvent::MouseButtonPress, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::LeftButton);
        send(QEvent::MouseButtonDblClick, Qt::LeftButton);
    });

    EXPECT_EQ(seen, 1);
}

TEST_F(Swatch, RightClickIsLeftToTheContextMenu)
{
    // Правой кнопкой открывают меню строки, и переключать ею видимость нельзя.
    const int seen = countOf(&SeriesSwatchDelegate::visibilityToggled,
                             [this] { click(Qt::RightButton); });

    EXPECT_EQ(seen, 0);
}

TEST_F(Swatch, PaintingLeavesTheStoredStateAlone)
{
    // Делегат только рисует: состояние живёт в модели, и отрисовка не вправе его трогать.
    QPixmap canvas(200, 100);
    canvas.fill(Qt::black);
    table.render(&canvas);

    EXPECT_EQ(table.item(0, 0)->data(Qt::CheckStateRole).toInt(), Qt::Checked);
}

TEST_F(Swatch, HiddenAndVisibleLookDifferent)
{
    // Выключенный ряд обязан отличаться от включённого на глаз. Прежде он гасился
    // прозрачностью и на тёмной теме сливался с фоном до неразличимости.
    const auto renderWith = [this](Qt::CheckState state) {
        table.item(0, 0)->setData(Qt::CheckStateRole, state);
        QPixmap canvas(table.size());
        canvas.fill(Qt::black);
        table.render(&canvas);
        return canvas.toImage();
    };

    const QImage checked = renderWith(Qt::Checked);
    const QImage unchecked = renderWith(Qt::Unchecked);

    EXPECT_NE(checked, unchecked);
}
