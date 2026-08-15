/**
 * \file PlotterPanel.h
 * \brief Панель графика: таблица рядов, настройки и сам график.
 */
#pragma once

#include <spotty/ui/PanelWidget.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace spotty {

class PlotCanvas;
class PlotModel;
class PlotViewState;

/**
 * \class PlotterPanel
 * \brief Что показывать на графике, из чего его строить и куда выгрузить.
 *
 * \par Таблица рядов
 *
 * Строится сама по пришедшим данным: колонок в потоке столько, сколько шлёт устройство, и
 * заранее их не знает никто. В таблице ряд включается и выключается, ему задаётся цвет и
 * видна статистика по накопленному окну.
 *
 * Она же заменяет легенду. Прежде ряды различались только цветом — это нарушало
 * требование WCAG «не полагаться на цвет» и просто не позволяло понять, какая кривая чья.
 */
class PlotterPanel : public PanelWidget
{
    Q_OBJECT

public:
    PlotterPanel(IPanelHost *panelHost, PlotModel *model, PlotViewState *view,
                 QWidget *parent = nullptr);

protected:
    void settingsReset() override;

private:
    /**
     * \brief Как часто обновляются числа в таблице рядов, мс.
     *
     * Пять раз в секунду. Статистика считается обходом всего окна по каждому ряду, а
     * записывается в QTableWidget, который на каждую правку пересчитывает ширины колонок
     * (заголовок стоит в ResizeToContents). На каждую пришедшую строку это давало десятки
     * тысяч операций в секунду — при том, что человек всё равно не читает числа быстрее
     * нескольких раз в секунду.
     */
    static constexpr int kStatisticsIntervalMs = 200;

    void reloadFromSettings();
    void commit();

    /// \brief Перестроить таблицу под текущий состав рядов.
    void rebuildTable();

    /// \brief Обновить в таблице статистику, не трогая её строение.
    void refreshStatistics();

    /// \brief Завести таймер обновления статистики, если он ещё не идёт.
    void scheduleStatistics();

    /// \brief Открыть график в отдельном окне.
    void openInWindow();

    void exportCsv();
    void exportImage();

    PlotModel *m_model = nullptr;
    PlotViewState *m_view = nullptr;
    PlotCanvas *m_chart = nullptr;

    QComboBox *m_separator = nullptr;
    QSpinBox *m_points = nullptr;
    QComboBox *m_xAxis = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_pause = nullptr;

    /// \brief Отдельное окно с графиком; создаётся по требованию и живёт до закрытия.
    QWidget *m_window = nullptr;

    /// \brief Ограничитель частоты пересчёта статистики; см. #kStatisticsIntervalMs.
    QTimer *m_statisticsTimer = nullptr;

    bool m_populating = false;
};

} // namespace spotty
