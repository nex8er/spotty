/**
 * \file PlotCanvas.h
 * \brief Поле графика: отрисовка рядов, сетки и перекрестия.
 */
#pragma once

#include <spotty/data/Decimator.h>
#include <spotty/data/PlotTransform.h>

#include <QHash>
#include <QWidget>

class QTimer;

namespace spotty {

class IPanelHost;
class PlotModel;
class PlotViewState;

/**
 * \class PlotCanvas
 * \brief Рисует накопленные ряды по общему состоянию вида.
 *
 * \par Один объект в трёх местах
 *
 * Холст создаётся трижды: миниатюрой в боковой панели, полосой вместо терминала и в
 * отдельном окне. Данные и состояние вида у всех трёх общие, поэтому это один плоттер,
 * показанный в трёх местах, а не три разных графика.
 *
 * \par Сглаживание выключено безусловно
 *
 * Замер: одна и та же кривая из 200 точек рисуется 2.9 мс гладкой и 676 мс дёрганой — цена
 * зависит не от числа точек, а от длины пути пера. Дешёвого признака «здесь оно по карману»
 * не нашлось, а разброс — тысячекратный.
 *
 * \par Шкала у каждого ряда своя
 *
 * Ряды меряют разные величины. Одна шкала на всех прижимала бы милливольты к нулю рядом с
 * оборотами в минуту, и половина рядов превращалась бы в прямую у края поля.
 */
class PlotCanvas : public QWidget
{
    Q_OBJECT

public:
    PlotCanvas(IPanelHost *host, PlotModel *model, PlotViewState *view,
               QWidget *parent = nullptr);

    /// \brief Сохранить снимок графика в файл PNG.
    bool saveImage(const QString &filePath);

    /// \brief Снимок графика как изображение — для буфера обмена.
    QPixmap snapshot() const;

    /**
     * \brief Объявить действия, которыми управляют графиком.
     *
     * Действия добавляются в сам виджет (QWidget::addAction), и приложение показывает их
     * кнопками в панели управления областью вывода, когда полоса активна. Отдельного
     * метода в IPanelHost для этого не нужно: QWidget::actions() уже есть, и знание о
     * графике остаётся внутри графика.
     */
    void createActions();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    /// \brief Наибольший интервал между перерисовками: 16 мс — это 60 кадров в секунду.
    static constexpr int kRepaintIntervalMs = 16;

    /**
     * \struct SeriesFrame
     * \brief Всё, что нужно знать о ряде в этом кадре.
     */
    struct SeriesFrame
    {
        int index = 0;
        Decimator::Result reduced;
        YScale scale;
    };

    /// \brief Прямоугольник поля графика без подписей осей.
    QRect plotArea() const;

    /// \brief Окно по времени, прижатое к накопленному.
    XTransform transformFor(const QRect &area);

    /// \brief Свести все видимые ряды и раздать им шкалы.
    QList<SeriesFrame> buildFrames(const QRect &area, const XTransform &transform) const;

    /// \brief Приложить общий вертикальный масштаб и сдвиг к пределам ряда.
    YScale applyVertical(const PlotScales::Range &range, const QRect &area) const;

    /// \brief Ряд, чья шкала подписана слева; -1, если рисовать нечего.
    int labelledSeries(const QList<SeriesFrame> &frames) const;

    void drawFrame(QPainter &painter, const QRect &area, const XTransform &transform,
                   const QList<SeriesFrame> &frames) const;
    void drawSeries(QPainter &painter, const QRect &area, const XTransform &transform,
                    const QList<SeriesFrame> &frames) const;
    void drawCursor(QPainter &painter, const QRect &area, const XTransform &transform,
                    const QList<SeriesFrame> &frames) const;

    /// \brief Пометить картинку устаревшей и завести таймер перерисовки, если он не идёт.
    void scheduleRepaint();

    IPanelHost *m_host = nullptr;
    PlotModel *m_model = nullptr;
    PlotViewState *m_view = nullptr;

    /// \brief Положение курсора в поле графика; -1, когда курсора нет.
    int m_cursorX = -1;

    QTimer *m_repaintTimer = nullptr;
    bool m_dirty = false;
};

} // namespace spotty
