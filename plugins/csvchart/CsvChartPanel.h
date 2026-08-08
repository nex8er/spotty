/**
 * \file CsvChartPanel.h
 * \brief Настройки графика в боковой рейке.
 */
#pragma once

#include <spotty/ui/PanelWidget.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace spotty {

class CsvSeries;

/**
 * \class CsvChartPanel
 * \brief Что показывать на графике и из чего его строить.
 *
 * Настройки правятся по ходу работы — разделитель приходится подбирать под конкретную
 * прошивку, — поэтому живут в панели, а не в общем диалоге.
 */
class CsvChartPanel : public PanelWidget
{
    Q_OBJECT

public:
    CsvChartPanel(IPanelHost *host, CsvSeries *series, QWidget *parent = nullptr);

protected:
    void settingsReset() override;

private:
    /// \brief Прочитать настройки и раздать их модели.
    void reloadFromSettings();

    /// \brief Записать настройки и применить их.
    void commit();

    void updateStats();

    CsvSeries *m_series = nullptr;

    QComboBox *m_separator = nullptr;
    QSpinBox *m_points = nullptr;
    QCheckBox *m_enabled = nullptr;
    QPushButton *m_clear = nullptr;
    QLabel *m_stats = nullptr;

    /// \brief Признак программного заполнения — гасит обработчики.
    bool m_populating = false;
};

} // namespace spotty
