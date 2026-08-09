/**
 * \file CsvChartView.h
 * \brief Виджет графика: рисует накопленные ряды.
 */
#pragma once

#include <QWidget>

class QAction;

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
    /// \brief Прямоугольник поля графика без подписей осей.
    QRect plotArea() const;

    /// \brief Нарисовать сетку, подписи пределов и ось X.
    void drawFrame(QPainter &painter, const QRect &area, double minimum, double maximum) const;

    /// \brief Нарисовать перекрестие и значения рядов в точке курсора.
    void drawCursor(QPainter &painter, const QRect &area, double minimum, double maximum) const;

    IPanelHost *m_host = nullptr;
    CsvSeries *m_series = nullptr;

    bool m_paused = false;

    /// \brief Положение курсора в поле графика; -1, когда курсора нет.
    int m_cursorX = -1;

    QAction *m_pauseAction = nullptr;
};

} // namespace spotty
