/**
 * \file MacrosPanel.h
 * \brief Панель макросов: таблица команд, горячие клавиши, повторная отправка.
 */
#pragma once

#include <MacroStore.h>

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QShortcut;
class QTableWidget;
class QTimer;
class QToolButton;

namespace spotty {

struct AppContext;

/**
 * \class MacrosPanel
 * \brief Заготовленные посылки с горячими клавишами и повторной отправкой.
 *
 * \par Таблица вместо списка и диалога
 *
 * Имени у макроса нет: им служит сама команда. Придумывать имя для «AT+RST» пришлось бы
 * при каждом добавлении, и любое имя было бы хуже самой команды. По той же причине нет и
 * диалога правки — всё меняется прямо в таблице двойным щелчком или через меню по правой
 * кнопке. Формат и терминация живут в этом меню группами с отметкой: их значения
 * взаимоисключающие, и список с точкой напротив выбранного читается быстрее, чем
 * выпадающий список в ячейке.
 *
 * \par Горячие клавиши
 *
 * Новый макрос получает первую свободную клавишу из ряда F1…F12. Эти клавиши не отнимают
 * сочетаний у самой программы и не требуют модификатора. Когда заняты все двенадцать,
 * умолчание пустое: назначать тринадцатому что-то вроде «Ctrl+Shift+F1» программа не
 * вправе — такое сочетание пользователь выбирает сам.
 *
 * \par Периодическая отправка
 *
 * Период идёт по логарифмической шкале от 1 мс до 60 с: между ними шестьдесят тысяч
 * шагов, и равномерная шкала была бы бесполезна.
 *
 * \warning Периоды меньше 10 мс недостижимы точно: гранулярность таймеров операционной
 *          системы составляет от 1 до 15 мс. Панель показывает фактически достигнутый
 *          интервал рядом с заданным, чтобы цифра в списке не вводила в заблуждение.
 */
class MacrosPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MacrosPanel(const AppContext &context, QWidget *parent = nullptr);
    ~MacrosPanel() override;

    /// \brief Разрешить отправку. Выключается, когда канал закрыт.
    void setSendEnabled(bool enabled);

Q_SIGNALS:
    /// \brief Требуется отправить готовые байты.
    void sendRequested(const QByteArray &data);

    /// \brief Сообщение для строки состояния.
    void statusMessage(const QString &message);

private:
    /// \brief Колонки таблицы.
    enum Column {
        ColumnCommand = 0, ///< Команда; она же подпись макроса.
        ColumnShortcut,    ///< Сочетание клавиш.
        ColumnSend,        ///< Кнопка немедленной отправки.
        ColumnCount,
    };

    void reloadPresets();
    void reloadMacros();

    /// \brief Заполнить одну строку таблицы из макроса.
    void fillRow(int row, const Macro &macro);

    /// \brief Собрать макросы обратно из таблицы и сохранить набор.
    void commitTable();

    /// \brief Перерегистрировать горячие клавиши по текущему списку макросов.
    void rebuildShortcuts();

    /// \brief Отправить макрос по номеру строки.
    void sendMacro(int index);

    void addMacro();
    void removeSelected();
    void duplicateSelected();
    void importMacros();
    void exportMacros();

    /// \brief Меню по правой кнопке: на строке — правка, на пустом месте — создание.
    void showContextMenu(const QPoint &position);

    void startPeriodic();
    void stopPeriodic();

    /// \brief Обновить надпись о фактически достигнутом периоде.
    void updateActualInterval();

    /// \brief Пересобрать раскрашенные значки. Вызывается при смене темы.
    void updateIcons();

    const AppContext &m_context;
    MacroStore m_store;

    QComboBox *m_presetCombo = nullptr;
    QToolButton *m_addPresetButton = nullptr;
    QToolButton *m_removePresetButton = nullptr;

    QTableWidget *m_table = nullptr;
    QToolButton *m_addButton = nullptr;
    QToolButton *m_removeButton = nullptr;
    QToolButton *m_exportButton = nullptr;
    QToolButton *m_importButton = nullptr;

    QComboBox *m_periodicMacro = nullptr;
    QComboBox *m_periodInterval = nullptr;
    QToolButton *m_periodicButton = nullptr;
    QLabel *m_actualLabel = nullptr;

    QTimer *m_periodicTimer = nullptr;
    QList<QShortcut *> m_shortcuts;

    /// \brief Счётчик отправок и точка отсчёта — для измерения реального периода.
    qint64 m_periodicStartedMs = 0;
    int m_periodicCount = 0;

    bool m_sendEnabled = false;

    /// \brief Признак программного заполнения таблицы — гасит обработчик изменений.
    bool m_populating = false;
};

} // namespace spotty
