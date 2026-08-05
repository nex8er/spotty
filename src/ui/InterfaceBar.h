/**
 * \file InterfaceBar.h
 * \brief Верхняя полоса: состояние, выбор интерфейса, кнопка настроек.
 */
#pragma once

#include <spotty/api/ChannelState.h>

#include <QWidget>

class QLabel;
class QToolButton;

namespace spotty {

struct AppContext;
struct InterfaceEntry;
class InterfaceCombo;

/**
 * \class InterfaceBar
 * \brief Полоса вверху окна: индикатор состояния, выбор интерфейса и вход в его настройки.
 *
 * \par Содержимое списка
 *
 * Первым пунктом всегда идёт «Not selected», затем устройства, сгруппированные по
 * плагину-владельцу невыбираемыми строками-заголовками (spotty::InterfaceItemDelegate):
 * однотипные порты не перемешиваются между собой. Внутри группы — присутствующие обычным
 * цветом, запомненные, но отсутствующие — приглушённым; настроенный порт, который сейчас
 * не подключён, остаётся на виду, а не исчезает бесследно.
 *
 * Каждый пункт рисуется в две зоны: interfacePrimaryLabel() — псевдоним или имя устройства
 * от драйвера слева (общая функция, той же подписью пользуется и переключатель устройств
 * в настройках интерфейса), secondaryText() — системный адрес и время обнаружения
 * приглушённым цветом справа.
 *
 * \par Перестроение и выбор пользователя
 *
 * Список перестраивается по сигналу spotty::InterfaceRegistry::changed(), то есть
 * потенциально раз в секунду при опросе. Выбранный пункт при этом сохраняется, а сигналы
 * QComboBox глушатся: иначе перестроение под курсором сбрасывало бы выбор и порождало
 * ложные срабатывания interfaceSelected().
 */
class InterfaceBar : public QWidget
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param context Службы приложения; ссылка должна пережить виджет.
     * \param parent Родительский виджет.
     */
    explicit InterfaceBar(const AppContext &context, QWidget *parent = nullptr);

    /// \return Идентификатор выбранного интерфейса; пустая строка — «не выбрано».
    QString currentInterfaceId() const;

    /**
     * \brief Выбрать интерфейс по идентификатору.
     * \param id Идентификатор; неизвестный или пустой выбирает «не выбрано».
     */
    void setCurrentInterfaceId(const QString &id);

    /**
     * \brief Обновить индикатор состояния канала.
     * \param state Новое состояние.
     * \param detail Пояснение; показывается во всплывающей подсказке.
     */
    void setChannelState(ChannelState state, const QString &detail = {});

    /**
     * \brief Показать индикатор ошибки, не меняя состояния канала.
     *
     * Для случаев вроде ошибки кадрирования на уже открытом порту: канал остаётся
     * `Open`, само по себе это не смена состояния, но пользователь должен увидеть, что
     * что-то пошло не так. Снимается следующим вызовом setChannelState().
     * \param detail Текст ошибки; показывается во всплывающей подсказке.
     */
    void flagError(const QString &detail);

Q_SIGNALS:
    /// \brief Пользователь выбрал другой интерфейс. Пустая строка — «не выбрано».
    void interfaceSelected(const QString &id);

    /// \brief Нажата кнопка настроек интерфейса.
    void settingsRequested(const QString &id);

    /// \brief Нажата кнопка открытия или закрытия канала.
    void toggleOpenRequested();

protected:
    /// \brief Гасит прокрутку колесом над списком интерфейсов.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// \brief Перестроить список из реестра, сохранив текущий выбор.
    void rebuild();

    /// \brief Обновить значок и подпись состояния.
    void updateStatusIndicator();

    /// \brief Пересобрать раскрашенные значки. Вызывается при смене темы.
    void updateIcons();

    /**
     * \brief Служебные сведения: то, что рисуется справа приглушённым цветом.
     * \return Системный адрес и время обнаружения (или пометка о недоступности),
     *         разделённые «·».
     */
    QString secondaryText(const InterfaceEntry &entry) const;

    /// \brief Добавить невыбираемую строку-заголовок группы плагина.
    void addGroupHeader(const QString &label);

    const AppContext &m_context;
    QLabel *m_statusDot = nullptr;        ///< Цветной кружок состояния.
    InterfaceCombo *m_combo = nullptr;    ///< Выбор интерфейса.
    QToolButton *m_openButton = nullptr;  ///< Открыть или закрыть канал.
    QToolButton *m_settingsButton = nullptr; ///< Переход в настройки интерфейса.

    ChannelState m_state = ChannelState::Closed; ///< Отображаемое состояние.
    QString m_stateDetail;                ///< Пояснение к состоянию.

    bool m_hasError = false;              ///< Индикатор временно показывает ошибку.
    QString m_errorDetail;                ///< Текст ошибки для всплывающей подсказки.

    /// \brief Признак идущего перестроения — глушит обработчик смены пункта.
    bool m_rebuilding = false;
};

} // namespace spotty
