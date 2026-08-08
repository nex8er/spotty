/**
 * \file CsvChartPanel.h
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

namespace spotty {

class CsvChartView;
class CsvSeries;

/**
 * \class CsvChartPanel
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
class CsvChartPanel : public PanelWidget
{
    Q_OBJECT

public:
    CsvChartPanel(IPanelHost *host, CsvSeries *series, QWidget *parent = nullptr);

protected:
    void settingsReset() override;

private:
    void reloadFromSettings();
    void commit();

    /// \brief Перестроить таблицу под текущий состав рядов.
    void rebuildTable();

    /// \brief Обновить в таблице статистику, не трогая её строение.
    void refreshStatistics();

    /// \brief Открыть график в отдельном окне.
    void openInWindow();

    void exportCsv();
    void exportImage();

    CsvSeries *m_series = nullptr;
    CsvChartView *m_chart = nullptr;

    QComboBox *m_separator = nullptr;
    QSpinBox *m_points = nullptr;
    QComboBox *m_xAxis = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_pause = nullptr;

    /// \brief Отдельное окно с графиком; создаётся по требованию и живёт до закрытия.
    QWidget *m_window = nullptr;

    bool m_populating = false;
};

} // namespace spotty
