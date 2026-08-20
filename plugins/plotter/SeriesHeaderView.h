/**
 * \file SeriesHeaderView.h
 * \brief Заголовок таблицы рядов: флажок «показать или скрыть все» в колонке цвета.
 */
#pragma once

#include <QHeaderView>

#include <functional>

namespace spotty {

/**
 * \class SeriesHeaderView
 * \brief Заголовок с флажком, нарисованным в колонке цвета.
 *
 * \par Почему рисуем сами, а не кладём QCheckBox поверх заголовка
 *
 * Виджет, положенный на заголовок, выровнять по колонке нельзя надёжно. У QCheckBox без
 * подписи размер виджета и размер самого квадратика внутри него — разные величины: стиль
 * резервирует поля под текст, которого нет. Снаружи видно только первое, поэтому
 * центрирование по геометрии виджета сажает квадратик мимо середины колонки, а попытка
 * ужать виджет до 16 px отнимает место у индикатора, и тот теряет скругление. Обе беды —
 * следствие одного: расстояние от края виджета до края квадратика чужое и меняется со
 * стилем.
 *
 * Здесь квадратик рисуется теми же формулами, что и в spotty::SeriesSwatchDelegate:
 * сторона берётся у стиля через `PM_IndicatorWidth`, центр — центр своего прямоугольника.
 * Прямоугольники у заголовка и у ячейки делят одну колонку, поэтому совпадение по
 * горизонтали получается само, а не подбором чисел.
 *
 * \par Почему без Q_OBJECT
 *
 * О нажатии сообщает #onToggled, а не сигнал: единственному слушателю — панели — сигнал
 * не нужен, а moc за собой он бы потянул.
 */
class SeriesHeaderView : public QHeaderView
{
public:
    /**
     * \param column Колонка, в которой рисуется флажок.
     * \param parent Таблица; заголовок отдают ей через QTableView::setHorizontalHeader().
     */
    SeriesHeaderView(int column, QWidget *parent = nullptr);

    /// \brief Состояние флажка. Перерисовывает свою колонку.
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }

    /// \brief Зовётся при нажатии на флажок; аргумент — новое состояние.
    std::function<void(bool)> onToggled;

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    /// \brief Квадратик флажка внутри прямоугольника колонки; та же математика, что у ячейки.
    QRect indicatorRect(const QRect &section) const;

    int m_column = 0;
    bool m_checked = true;
};

} // namespace spotty
