/**
 * \file CsvChartView.h
 * \brief Виджет графика: рисует накопленные ряды.
 */
#pragma once

#include <QPolygonF>
#include <QWidget>

class QAction;
class QTimer;

namespace spotty {

class CsvSeries;
class IPanelHost;

/**
 * \class CsvChartView
 * \brief Отрисовка рядов с сеткой, курсором и паузой.
 *
 * \par Обычный виджет, а не слой поверх терминала
 *
 * Прежде график рисовался поверх вывода. От этого отказались: текст под кривой читать
 * трудно, а сама кривая теряется в тексте — оба страдали ради экономии места, которой
 * никто не просил. Теперь это обычный виджет, и владелец волен показать его вместо
 * терминала либо в отдельном окне.
 *
 * \par Пауза
 *
 * Замораживает картинку, но не сбор данных: модель продолжает накапливать, а на экране
 * остаётся то, что нужно рассмотреть. Останавливать накопление означало бы терять данные
 * ровно тогда, когда за ними следят.
 *
 * \par Почему отрисовка не идёт по каждой строке
 *
 * Темп данных и темп показа разведены намеренно. Устройство шлёт строки с той частотой,
 * с какой ему удобно — тысячами в секунду; экран обновляется 60 раз в секунду, и
 * нарисовать больше кадров, чем он покажет, физически невозможно. Пока перерисовка стояла
 * на сигнале модели напрямую, поток интерфейса уходил в неё целиком и окно переставало
 * отвечать. Теперь сигнал только помечает картинку устаревшей, а перерисовку заводит
 * таймер не чаще #kRepaintIntervalMs.
 *
 * \par Прореживание
 *
 * В поле шириной 1200 пикселей нельзя показать 5000 точек: на колонку пикселей приходится
 * четыре точки, и три из них рисуются поверх четвёртой. seriesPolyline() сводит каждую
 * колонку к паре «наименьшее и наибольшее» — это ровно то, что осталось бы на экране от
 * честного обхода всех точек, включая одиночные выбросы, но геометрии в разы меньше.
 *
 * \par Сглаживание
 *
 * Кривые рисуются без него, и это главное решение по скорости — подробности и цифры в
 * paintEvent(). Коротко: цена сглаживания зависит от длины пути пера, а не от числа точек,
 * и разброс достигает трёх порядков, поэтому надёжного правила «здесь оно по карману» нет.
 */
class CsvChartView : public QWidget
{
    Q_OBJECT

public:
    CsvChartView(IPanelHost *host, CsvSeries *series, QWidget *parent = nullptr);

    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }

    /// \brief Сохранить снимок графика в файл PNG.
    bool saveImage(const QString &filePath);

    /**
     * \brief Объявить действия, которыми управляют графиком.
     *
     * Действия добавляются в сам виджет (QWidget::addAction), и приложение показывает их
     * кнопками в панели управления областью вывода, когда полоса активна. Отдельного
     * метода в IPanelHost для этого не нужно: QWidget::actions() уже есть, и знание о
     * графике остаётся внутри графика.
     */
    void createActions();

Q_SIGNALS:
    /// \brief Пауза переключена изнутри — двойным щелчком по полю графика.
    void pausedChanged(bool paused);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    /// \brief Наибольший интервал между перерисовками: 16 мс — это 60 кадров в секунду.
    static constexpr int kRepaintIntervalMs = 16;

    /// \brief Прямоугольник поля графика без подписей осей.
    QRect plotArea() const;

    /// \brief Нарисовать сетку, подписи пределов и ось X.
    void drawFrame(QPainter &painter, const QRect &area, double minimum, double maximum) const;

    /// \brief Нарисовать перекрестие и значения рядов в точке курсора.
    void drawCursor(QPainter &painter, const QRect &area, double minimum, double maximum) const;

    /**
     * \brief Точки ряда, сведённые к разрешению поля графика.
     * \param values Значения ряда от старого к новому.
     * \param area Поле графика в координатах виджета.
     * \param minimum Нижняя граница вертикальной шкалы.
     * \param span Высота шкалы; вызывающий гарантирует, что она не ноль.
     *
     * Пока точек меньше, чем пикселей по ширине, отдаёт их как есть. Дальше на каждую
     * колонку пикселей приходится по паре точек — наибольшее и наименьшее значение из
     * попавших в колонку. Форма кривой и выбросы при этом сохраняются полностью: разрешения
     * экрана всё равно не хватило бы, чтобы показать больше.
     */
    QPolygonF seriesPolyline(const QList<double> &values, const QRect &area, double minimum,
                             double span) const;

    /// \brief Пометить картинку устаревшей и завести таймер перерисовки, если он не идёт.
    void scheduleRepaint();

    IPanelHost *m_host = nullptr;
    CsvSeries *m_series = nullptr;

    bool m_paused = false;

    /// \brief Положение курсора в поле графика; -1, когда курсора нет.
    int m_cursorX = -1;

    QAction *m_pauseAction = nullptr;

    /// \brief Ограничитель частоты перерисовки; см. «Почему отрисовка не идёт по каждой строке».
    QTimer *m_repaintTimer = nullptr;

    /// \brief Данные менялись с последней перерисовки.
    bool m_dirty = false;
};

} // namespace spotty
