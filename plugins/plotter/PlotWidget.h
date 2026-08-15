/**
 * \file PlotWidget.h
 * \brief Плоттер целиком: поле графика и ряд кнопок под ним.
 */
#pragma once

#include <QWidget>

class QToolButton;

namespace spotty {

class IPanelHost;
class PlotCanvas;
class PlotModel;
class PlotViewState;

/**
 * \class PlotWidget
 * \brief Поле графика вместе с управлением им.
 *
 * \par Один композит на три места
 *
 * Это и есть «единый объект»: миниатюра в боковой панели, полоса вместо терминала и
 * отдельное окно — три экземпляра одного класса поверх одних данных и одного состояния
 * вида. Правка здесь меняет все три сразу; расходиться им больше нечем.
 *
 * Кнопки только значками с подсказкой — как в остальной программе: три подписи в узкой
 * панели обрезались до неразличимых огрызков, и смысл всё равно нёс бы не текст.
 */
class PlotWidget : public QWidget
{
    Q_OBJECT

public:
    /// \brief Где показан плоттер. Влияет только на состав кнопок.
    enum class Placement {
        Panel,  ///< Миниатюра в боковой панели.
        Strip,  ///< Полоса вместо терминала.
        Window, ///< Отдельное окно: кнопки «открыть в окне» здесь уже незачем.
    };

    PlotWidget(IPanelHost *panelHost, PlotModel *model, PlotViewState *view,
               Placement placement, QWidget *parent = nullptr);

    PlotCanvas *canvas() const { return m_canvas; }

Q_SIGNALS:
    /// \brief Нажата кнопка «открыть в окне»; окно единственное, и владеет им плагин.
    void openInWindowRequested();

private:
    /// \brief Кнопка в стиле программы: значок, подсказка, без подписи.
    QToolButton *makeButton(char32_t glyph, const QString &tip);

    /// \brief Обновить значок и подсказку кнопки паузы под текущее состояние.
    void updatePlayPause();

    /// \brief Меню выбора разделителя — по левому клику, как просил владелец.
    void showSeparatorMenu();

    /// \brief Меню выбора колонки для оси X.
    void showXAxisMenu();

    /// \brief Снимок в буфер обмена; правый клик по той же кнопке предложит файл.
    void copyImage();
    void saveImage();
    void exportCsv();

    IPanelHost *m_host = nullptr;
    PlotModel *m_model = nullptr;
    PlotViewState *m_view = nullptr;

    PlotCanvas *m_canvas = nullptr;
    QToolButton *m_playPause = nullptr;
    QToolButton *m_follow = nullptr;
};

} // namespace spotty
