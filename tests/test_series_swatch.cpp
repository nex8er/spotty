/**
 * \file test_series_swatch.cpp
 * \brief Тесты первой колонки таблицы рядов: доходит ли щелчок до делегата.
 */
#include "SeriesSwatchDelegate.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QMouseEvent>
#include <QStyle>
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

TEST_F(Swatch, StandardCheckIndicatorIsStrippedFromTheStyleOption)
{
    // Проверяется сам механизм, а не картинка: раскладка штатного флажка зависит от стиля
    // и таблицы стилей, и попытка поймать его по пикселям в голой тестовой таблице
    // проходила одинаково и с исправлением, и без него — то есть не доказывала ничего.
    //
    // Признак обязан сниматься именно в initStyleOption(): QStyledItemDelegate::paint()
    // зовёт его заново на переданных параметрах, поэтому всё, что снято до вызова базового
    // метода, возвращается обратно. Так системный флажок и оказывался поверх квадратика.
    class Probe : public SeriesSwatchDelegate
    {
    public:
        using SeriesSwatchDelegate::initStyleOption;
    };

    Probe probe;
    QStyleOptionViewItem option;
    option.widget = &table;

    probe.initStyleOption(&option, table.model()->index(0, 0));

    EXPECT_FALSE(option.features.testFlag(QStyleOptionViewItem::HasCheckIndicator));
    EXPECT_TRUE(option.text.isEmpty());
}

TEST_F(Swatch, SwatchMatchesTheStandardIndicatorSize)
{
    // Квадратик стоит на месте штатного флажка и обязан быть с него ростом, иначе строка
    // выглядит съехавшей.
    QStyleOptionViewItem option;
    option.widget = &table;
    const int indicator =
        table.style()->pixelMetric(QStyle::PM_IndicatorWidth, &option, &table);

    const QSize hint = delegate.sizeHint(option, table.model()->index(0, 0));

    EXPECT_GE(hint.width(), indicator);
    EXPECT_GE(hint.height(), indicator);
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
