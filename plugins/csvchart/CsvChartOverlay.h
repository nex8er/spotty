/**
 * \file CsvChartOverlay.h
 * \brief График поверх области вывода терминала.
 */
#pragma once

#include <spotty/ui/PanelWidget.h>

namespace spotty {

class CsvSeries;

/**
 * \class CsvChartOverlay
 * \brief Рисует накопленные ряды поверх текста терминала.
 *
 * \par Почему слой, а не полоса
 *
 * График здесь — не отдельная область, а второй взгляд на тот же поток: видно и цифры
 * строками, и их форму. Полоса в разделителе отняла бы у вывода высоту, а слой ничего не
 * отнимает.
 *
 * Полупрозрачность даёт сам слой: собственного фона у него нет, поэтому текст под
 * графиком остаётся читаемым.
 */
class CsvChartOverlay : public PanelWidget
{
    Q_OBJECT

public:
    CsvChartOverlay(IPanelHost *host, CsvSeries *series, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

    /// \brief Цвета сетки и подписей берутся из темы, поэтому нужна перерисовка.
    void themeChanged() override;

private:
    /// \brief Цвет ряда по номеру.
    QColor seriesColor(int index) const;

    CsvSeries *m_series = nullptr;
};

} // namespace spotty
