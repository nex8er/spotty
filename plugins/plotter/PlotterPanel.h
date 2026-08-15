/**
 * \file PlotterPanel.h
 * \brief Панель графика: таблица рядов, настройки и сам график.
 */
#pragma once

#include <spotty/data/PlotProfile.h>

#include <spotty/ui/PanelWidget.h>

class QComboBox;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace spotty {

class PlotCanvas;
class PlotModel;
class PlotWidget;
class PlotViewState;
class SeriesSwatchDelegate;

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

Q_SIGNALS:
    /// \brief Просьба показать плоттер отдельным окном; окно единственное, им владеет плагин.
    void openInWindowRequested();

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

    /// \brief Задержка автосохранения профиля, мс.
    static constexpr int kProfileSaveDelayMs = 400;

    void reloadFromSettings();
    void commit();

    /// \brief Перестроить таблицу под текущий состав рядов.
    void rebuildTable();

    /// \brief Обновить в таблице статистику, не трогая её строение.
    void refreshStatistics();

    /// \brief Завести таймер обновления статистики, если он ещё не идёт.
    void scheduleStatistics();

    /**
     * \brief Пересчитать число знаков в колонках статистики по их ширине.
     *
     * Зовётся при изменении ширины и никогда — по приходу данных. Именно это и держит
     * цифры неподвижными: они меняются, только когда панель тянут за край.
     */
    void updateStatisticsWidth();

    /// \brief Отразить в таблице активный ряд, не трогая выделение.
    void syncActiveRow();

    /// \brief Спросить цвет ряда и применить его.
    void pickColour(int row);

    /// \brief Меню строки: цвет, переименование, пределы шкалы, очистка одной колонки.
    void showTableMenu(const QPoint &at);

    /// \brief Спросить пользовательские пределы шкалы ряда.
    void editRange(int row);

    /// \name Профили
    /// @{

    /// \brief Перечитать список профилей в выпадающем списке.
    void reloadProfiles();

    /// \brief Собрать нынешние настройки в профиль под именем \p name.
    PlotProfile currentProfile(const QString &name) const;

    /// \brief Применить профиль к модели и виду.
    void applyProfile(const PlotProfile &profile);

    /// \brief Записать текущий профиль на диск; вызывается отложенно.
    void saveProfile();

    /// \brief Завести таймер автосохранения, если он ещё не идёт.
    void scheduleProfileSave();

    void addProfile();
    void deleteProfile();

    /**
     * \brief Подобрать профиль под нынешний состав колонок.
     *
     * Зовётся, когда состав рядов изменился: до первых данных подбирать не по чему.
     * Уже выбранный вручную профиль не трогается — иначе автоподбор отменял бы выбор
     * человека при каждой новой колонке.
     */
    void autoSelectProfile();
    /// @}

    PlotModel *m_model = nullptr;
    PlotViewState *m_view = nullptr;
    PlotWidget *m_plot = nullptr;

    QComboBox *m_profiles = nullptr;
    QSpinBox *m_points = nullptr;
    QTableWidget *m_table = nullptr;
    SeriesSwatchDelegate *m_swatch = nullptr;

    /// \brief Сколько значащих цифр показывать в статистике; см. updateStatisticsWidth().
    int m_statisticsDigits = 5;

    /// \brief Ограничитель частоты пересчёта статистики; см. #kStatisticsIntervalMs.
    QTimer *m_statisticsTimer = nullptr;

    /**
     * \brief Откладывает запись профиля.
     *
     * Настройки правят подряд — цвет, имя, пределы, — и писать файл на каждое нажатие
     * значило бы дёргать диск десятки раз за секунду. Одиночный: когда ничего не меняют,
     * ничего и не тикает.
     */
    QTimer *m_profileTimer = nullptr;

    /// \brief Хранилище профилей в каталоге плагина.
    PlotProfileStore m_store;

    /// \brief Выбранный профиль; пусто, пока ни один не выбран.
    QString m_currentProfile;

    /// \brief Профиль выбран человеком — автоподбор его больше не трогает.
    bool m_profileChosenByUser = false;

    bool m_populating = false;
};

} // namespace spotty
