/**
 * \file IPanelHost.h
 * \brief Всё, что приложение предоставляет панельному плагину.
 */
#pragma once

#include <spotty/api/ChannelState.h>
#include <spotty/api/DataDirection.h>
#include <spotty/data/DataCodec.h>
#include <spotty/data/HighlightRules.h>
#include <spotty/ui/PanelDescriptor.h>
#include <spotty/ui/SpottyUiApiExport.h>

#include <QColor>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>

namespace spotty {

/**
 * \class IPanelHost
 * \brief Приложение с точки зрения панели.
 *
 * \par Зачем
 *
 * Встроенные панели получают структуру служб с сырыми указателями на Session,
 * SettingsStore, ThemeManager и InterfaceRegistry. Плагину её отдать нельзя: все эти типы
 * живут в `spotty-core`, куда ему дороги нет, и половина их возможностей панели не нужна,
 * а вреда наделать позволяет. Хост — это та же структура служб, сведённая к тому, что
 * панели действительно требуется, и выраженная в типах SDK.
 *
 * \par Время жизни
 *
 * Один экземпляр на плагин, создаётся приложением до первой панели и живёт дольше всех
 * её панелей. Плагин хост не удаляет.
 *
 * \par Потоки
 *
 * Всё — в потоке пользовательского интерфейса. Вызывать методы из своего потока панели
 * нельзя; для этого есть обычные средства Qt.
 */
class SPOTTY_UI_API_EXPORT IPanelHost : public QObject
{
    Q_OBJECT

public:
    /**
     * \enum ColorRole
     * \brief Роль цвета в оформлении.
     *
     * \par Почему перечисление, а не структура цветов
     *
     * Структура заморозила бы в ABI свою раскладку: добавление цвета в тему сломало бы
     * уже собранные панели. Перечисление растёт в конец безболезненно.
     *
     * Спрашивать цвет нужно только для собственной отрисовки. Обычные виджеты панели
     * оформляются общей таблицей стилей сами — там действуют те же селекторы `#panelTitle`,
     * `#hintLabel`, `#card`, что и во встроенных панелях.
     *
     * \warning Значения дописываются только в конец.
     */
    enum class ColorRole {
        Window,        ///< Фон приложения.
        Panel,         ///< Боковые панели и полосы инструментов.
        Base,          ///< Фон терминала и полей ввода.
        Border,        ///< Разделительные линии.
        ControlBorder, ///< Контур поля ввода; к фону даёт не меньше 3:1.
        Text,
        TextMuted,     ///< Второстепенные подписи и метки времени.
        Accent,        ///< Акцент для контуров.
        AccentText,    ///< Текст поверх залитого акцентом фона.
        AccentSolid,   ///< Заливка главной кнопки.
        Selection,
        Hover,
        Pressed,
        Rx,            ///< Принятые данные.
        Tx,            ///< Отправленные данные.
        Error,
        Ok,
    };

    /**
     * \enum Metric
     * \brief Размер оформления, зависящий от операционной системы.
     *
     * \warning ControlHeight, ButtonMinWidth — размеры **содержимого**, а не всего
     *          элемента: отступы и рамка прибавляются сверху. Полная высота поля —
     *          `ControlHeight + 2*PadV + 2`.
     */
    enum class Metric {
        Gap,           ///< Шаг раскладки.
        Radius,        ///< Скругление полей и кнопок.
        CardRadius,    ///< Скругление карточек окна.
        ControlHeight,
        ButtonMinWidth,
        PadV,
        PadH,
        ScrollBarWidth,
        FontPointSize, ///< Кегль интерфейса; 0 — системный.
        TitlePointSize,
        SmallPointSize,
    };

    explicit IPanelHost(QObject *parent = nullptr);

    /**
     * \brief Деструктор.
     *
     * Объявлен здесь и определён в `.cpp` намеренно: это даёт классу ключевую функцию,
     * поэтому таблица виртуальных функций и typeinfo порождаются один раз — в
     * `libspotty-ui-api`, а не в каждом плагине заново. Та же причина, что у
     * spotty::IInterfaceChannel.
     */
    ~IPanelHost() override;

    /// \brief Идентификатор плагина, которому принадлежит этот хост.
    virtual QString pluginId() const = 0;

    /// \name Отправка
    /// @{

    /**
     * \brief Отправить байты в интерфейс.
     *
     * \note Вызов не блокирующий: байты кладутся в очередь потока ввода-вывода и уходят
     *       позже. Отправляя большой файл, не сваливайте его одним куском — очередь
     *       вырастет на весь объём, а окно перестанет отвечать. Принятый способ: послать
     *       порцию, дождаться её в dataLogged() с DataDirection::Tx, послать следующую.
     */
    virtual void send(const QByteArray &data) = 0;

    /// \brief Положить текст в строку отправки, не отправляя: пусть человек посмотрит.
    virtual void composeInSendBar(const QString &text, DataCodec::Format format) = 0;

    /// @}
    /// \name Состояние интерфейса
    /// @{

    virtual ChannelState channelState() const = 0;

    /// \return Идентификатор выбранного устройства; пустая строка — не выбрано.
    virtual QString interfaceId() const = 0;

    /// \return Системное имя устройства: `"COM5"`, `"/dev/ttyUSB0"`.
    virtual QString interfaceName() const = 0;

    /// \return Псевдоним, заданный пользователем, а если его нет — системное имя.
    virtual QString interfaceAlias() const = 0;

    /// @}
    /// \name Свои настройки и свои файлы
    /// @{

    /**
     * \brief Прочитать настройку плагина.
     *
     * Ключ разворачивается в `plugins/<pluginId>/<key>` — префикс подставляет хост, и
     * залезть в чужие настройки, ошибившись в строке, невозможно.
     */
    virtual QVariant value(const QString &key, const QVariant &fallback = {}) const = 0;

    virtual void setValue(const QString &key, const QVariant &value) = 0;

    /**
     * \brief Каталог плагина внутри конфигурации, `<конфигурация>/<pluginId>/`.
     *
     * Создаётся при первом обращении. Для того, что принадлежит плагину и правится им же:
     * пресеты, кэш, состояние.
     */
    virtual QString dataDir() const = 0;

    /**
     * \brief Каталог для файлов, которые пользователь откроет другой программой.
     *
     * Логи, выгрузки, снимки. В переносном режиме — рядом с приложением, иначе в
     * документах.
     */
    virtual QString documentsDir() const = 0;

    /// @}
    /// \name Общение с пользователем
    /// @{

    /// \brief Показать сообщение в строке состояния окна.
    virtual void showStatusMessage(const QString &message) = 0;

    /// \brief Сделать панель видимой и выбранной. Ничего не делает для неизвестного id.
    virtual void activatePanel(const QString &panelId) = 0;

    /**
     * \brief Объявить набор сочетаний клавиш целиком.
     *
     * Хост вычислит разницу с прежним набором сам, поэтому вызывать можно сколько угодно
     * раз. Срабатывание приходит сигналом shortcutActivated().
     */
    virtual void setShortcuts(const QList<PanelShortcut> &shortcuts) = 0;

    /// @}
    /// \name Запись в терминал
    /// @{

    /// \brief Добавить строку как сообщение самой программы (DataDirection::System).
    virtual void appendToTerminal(const QString &text) = 0;

    /**
     * \brief Добавить байты так, будто они пришли от устройства.
     *
     * Проходят разбор ANSI и попадают в HEX-режим наравне с настоящими. Для плагинов,
     * которые расшифровывают протокол и показывают результат рядом с сырым потоком.
     *
     * \note В цепочку преобразования не возвращаются: иначе звено, пишущее сюда из
     *       filterIncoming(), зациклило бы само себя.
     */
    virtual void injectReceived(const QByteArray &data) = 0;

    virtual void clearTerminal() = 0;

    /**
     * \brief Показать файл в области терминала вместо живого вывода.
     * \return `false`, если файл не удалось прочитать.
     *
     * Живой вывод не теряется и возвращается кнопкой.
     */
    virtual bool showDocument(const QString &filePath, const QString &title) = 0;

    /// @}
    /// \name Управление показом терминала
    /// @{

    virtual void setHighlightRules(const HighlightRules &rules) = 0;
    virtual void setSearchPattern(const QString &pattern, SearchOptions options) = 0;
    virtual void setFilterEnabled(bool enabled) = 0;
    virtual void findNext() = 0;
    virtual void findPrevious() = 0;

    /// \brief Сколько строк совпало с образцом поиска.
    virtual int matchCount() const = 0;

    /**
     * \brief Порядковый номер текущего совпадения, считая с единицы.
     * \return 0, если поиск не задан или переход ни разу не делали.
     *
     * Вместе с matchCount() даёт «23 из 435». Одного числа мало: сотня совпадений без
     * указания, где ты среди них, не отвечает ни на один вопрос, ради которого искали.
     */
    virtual int currentMatch() const = 0;

    /// @}
    /// \name Чтение разобранных строк
    /// @{

    /**
     * \brief Номер самой старой строки, ещё лежащей в буфере.
     *
     * Нумерация сквозная и никогда не сбрасывается, а буфер подрезается спереди — поэтому
     * номер строки означает одно и то же всё время её жизни, но старые номера перестают
     * быть доступными.
     */
    virtual qint64 firstLineNumber() const = 0;

    /// \brief Номер, который получит следующая добавленная строка.
    virtual qint64 nextLineNumber() const = 0;

    /**
     * \brief Прочитать строку по номеру.
     * \return `false`, если строка уже вытеснена из буфера.
     */
    virtual bool line(qint64 number, TerminalLine *out) const = 0;

    /// @}
    /// \name Оформление
    /// @{

    virtual QColor color(ColorRole role) const = 0;
    virtual int metric(Metric which) const = 0;
    virtual bool isDarkTheme() const = 0;

    /// \brief Значок из шрифта Material Design Icons, окрашенный по текущей теме.
    virtual QIcon icon(char32_t glyph, int size = 20) const = 0;

    /// \brief Он же приглушённым цветом — для второстепенных действий.
    virtual QIcon mutedIcon(char32_t glyph, int size = 20) const = 0;

    /// @}

Q_SIGNALS:
    /**
     * \brief Данные прошли через сессию — сырые, до цепочки преобразования.
     * \param direction Принято или отправлено.
     */
    void dataLogged(const QByteArray &data, spotty::DataDirection direction);

    /**
     * \brief Принятые байты вместе с отметкой чтения.
     *
     * Отдельно от dataLogged(), у которого отметки нет: у передающего направления времени
     * чтения не существует, и подставлять туда ноль означало бы завести «магическое»
     * значение.
     */
    void dataReceived(const QByteArray &data, qint64 monotonicNs);

    /// \brief В терминале появились строки; читать их через line().
    void terminalLinesAppended(qint64 firstLineNumber, qint64 count);

    void channelStateChanged(spotty::ChannelState state);
    void themeChanged();
    /**
     * \brief Набор совпадений или положение в нём изменились.
     * \param currentMatch Номер текущего совпадения с единицы; 0 — перехода не было.
     * \param totalMatches Всего совпавших строк.
     *
     * Оба числа в одном сигнале, а не в двух: «23 из 435» — это один факт, и разнесённый
     * по двум уведомлениям он неизбежно показывался бы промежуточным состоянием.
     */
    void matchCountChanged(int currentMatch, int totalMatches);

    /// \param shortcutId Полный идентификатор вида `"<pluginId>.<id>"`.
    void shortcutActivated(const QString &shortcutId);

    /// \brief Настройки сброшены к умолчаниям — перечитать своё состояние.
    void settingsReset();

    /// \brief Окно закрывается: записать несохранённое, остановить запись, закрыть файлы.
    void aboutToClose();

    /**
     * \brief Пользователь просит сохранить содержимое строки отправки.
     * \param text Набранное, как есть.
     * \param format Как оно трактуется.
     * \param termination Что дописывается в конец.
     *
     * Сигнал назван по событию, а не по тому, кто его примет: строка отправки не знает
     * ни о макросах, ни о любой другой панели, и связывать её с плагином напрямую
     * значило бы вернуть в окно поимённую связь, от которой ушли. Принять просьбу может
     * любая панель; сегодня это делает плагин макросов.
     */
    void sendBarSaveRequested(const QString &text, spotty::DataCodec::Format format,
                              spotty::DataCodec::Termination termination);
};

} // namespace spotty
