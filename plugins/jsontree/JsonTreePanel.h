/**
 * \file JsonTreePanel.h
 * \brief Панель разбора JSON: дерево, кнопки управления, строка состояния.
 */
#pragma once

#include <spotty/ui/PanelWidget.h>

class QLabel;
class QTimer;
class QToolButton;

namespace spotty {

class JsonFramer;
class JsonTreeModel;
class JsonTreeView;

/**
 * \class JsonTreePanel
 * \brief Что пришло в потоке JSON, с каким значением и как часто.
 *
 * \par Пауза относится к разбору, а не к терминалу
 *
 * Остановленное дерево не останавливает устройство и не трогает общий журнал: замирает
 * только то, что видно здесь. Та же развилка, что у паузы плоттера.
 *
 * \par Замолчавшее поле не удаляется само
 *
 * Пропавшее поле — ровно то, что человек ищет, открывая такую панель, и удалить его строку
 * значило бы уничтожить улику. Поэтому по умолчанию не исчезает ничего: замершие сереют,
 * их частота падает к нулю, а убрать их можно кнопкой, когда они действительно мешают.
 */
class JsonTreePanel : public PanelWidget
{
    Q_OBJECT

public:
    JsonTreePanel(IPanelHost *panelHost, JsonTreeModel *model, JsonFramer *framer,
                  QWidget *parent = nullptr);

    /// \brief Идёт ли разбор; плагин спрашивает это перед тем, как кормить модель.
    bool isPaused() const { return m_paused; }

Q_SIGNALS:
    /// \brief Пользователь переключил паузу разбора.
    void pauseChanged(bool paused);

protected:
    void themeChanged() override;
    void settingsReset() override;

private:
    /// \brief Как часто обновляется строка состояния, мс.
    static constexpr int kStatusIntervalMs = 200;

    /// \brief Пересобрать раскрашенные значки. Вызывается при смене темы.
    void updateIcons();

    /// \brief Обновить строку состояния и полосу предупреждения.
    void updateStatus();

    /// \brief Перечитать настройки вида; настройки модели применяет плагин.
    void applyViewSettings();

    /// \brief Меню строки: скопировать путь, значение или поддерево.
    void showTreeMenu(const QPoint &position);

    /**
     * \brief Меню кнопки вспышки: длительность свечения.
     *
     * Правая кнопка, а не отдельное поле в диалоге настроек: длительность подбирают
     * глазами, на живом потоке, и ходить за ней в диалог означало бы терять из виду то,
     * ради чего её меняют.
     */
    void showFlashMenu(const QPoint &position);

    /// \brief Записать длительность вспышки в настройки и применить к виду.
    void setFlashDuration(int milliseconds);

    JsonTreeModel *m_model = nullptr;
    JsonFramer *m_framer = nullptr;
    JsonTreeView *m_view = nullptr;

    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_clearButton = nullptr;
    QToolButton *m_hideStaleButton = nullptr;
    QToolButton *m_pruneButton = nullptr;
    QToolButton *m_expandButton = nullptr;
    QToolButton *m_collapseButton = nullptr;
    QToolButton *m_flashButton = nullptr;

    QLabel *m_status = nullptr;
    QLabel *m_warning = nullptr;

    QTimer *m_statusTimer = nullptr;

    bool m_paused = false;
};

} // namespace spotty
