/**
 * \file MacrosPanel.h
 * \brief Панель макросов: пресеты, горячие клавиши, периодическая отправка.
 */
#pragma once

#include <MacroStore.h>

#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QShortcut;
class QTimer;
class QToolButton;

namespace spotty {

struct AppContext;

/**
 * \class MacrosPanel
 * \brief Заготовленные посылки с горячими клавишами и повторной отправкой.
 *
 * \par Пресеты
 *
 * Сверху выбор набора. Каждый набор — отдельный файл, поэтому набор для конкретного
 * устройства переносится и передаётся целиком.
 *
 * \par Периодическая отправка
 *
 * Внизу выбор макроса и периода. Значения периода идут по логарифмической шкале от
 * 1 мс до 60 с: равномерная шкала на таком диапазоне бесполезна — между 1 мс и 60 с
 * шестьдесят тысяч шагов.
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
    void reloadPresets();
    void reloadMacros();

    /// \brief Перерегистрировать горячие клавиши по текущему списку макросов.
    void rebuildShortcuts();

    /// \brief Отправить макрос по индексу.
    void sendMacro(int index);

    void addMacro();
    void editMacro(QListWidgetItem *item);
    void removeMacro();

    void startPeriodic();
    void stopPeriodic();

    /// \brief Обновить надпись о фактически достигнутом периоде.
    void updateActualInterval();

    const AppContext &m_context;
    MacroStore m_store;

    QComboBox *m_presetCombo = nullptr;
    QToolButton *m_addPresetButton = nullptr;
    QToolButton *m_removePresetButton = nullptr;

    QListWidget *m_list = nullptr;
    QToolButton *m_addButton = nullptr;
    QToolButton *m_editButton = nullptr;
    QToolButton *m_removeButton = nullptr;

    QComboBox *m_periodicMacro = nullptr;
    QComboBox *m_periodInterval = nullptr;
    QPushButton *m_periodicButton = nullptr;
    QLabel *m_actualLabel = nullptr;

    QTimer *m_periodicTimer = nullptr;
    QList<QShortcut *> m_shortcuts;

    /// \brief Счётчик отправок и точка отсчёта — для измерения реального периода.
    qint64 m_periodicStartedMs = 0;
    int m_periodicCount = 0;

    bool m_sendEnabled = false;
};

} // namespace spotty
