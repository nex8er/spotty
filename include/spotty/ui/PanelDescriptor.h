/**
 * \file PanelDescriptor.h
 * \brief Описание того, что панельный плагин добавляет в окно.
 */
#pragma once

#include <spotty/api/DataDirection.h>

#include <QByteArray>
#include <QDateTime>
#include <QKeySequence>
#include <QString>

namespace spotty {

/**
 * \enum PanelPlacement
 * \brief Куда попадает виджет панели.
 */
enum class PanelPlacement {
    /// \brief Страница в боковой рейке значков — то же место, где живут макросы и поиск.
    Rail,

    /**
     * \brief Своя полоса над или под терминалом в общем вертикальном разделителе.
     *
     * В отличие от Overlay, ничего не закрывает: пользователь делит место мышью.
     */
    Splitter,

    /**
     * \brief Слой поверх области вывода.
     *
     * Для того, что должно читаться вместе с текстом и на его фоне, — графика, индикатора,
     * подсказки. Слой относится к живому выводу и прячется при просмотре файла лога.
     */
    Overlay,
};

/// \brief С какой стороны от терминала встаёт полоса. Значимо только для PanelPlacement::Splitter.
enum class PanelSide { Above, Below };

/// \brief Как слой прижат к области вывода. Значимо только для PanelPlacement::Overlay.
enum class PanelAnchor { Fill, TopLeft, TopRight, BottomLeft, BottomRight };

/**
 * \struct PanelDescriptor
 * \brief Одна панель, объявленная плагином.
 *
 * Агрегат C++20, как spotty::SettingsField: заполняется назначенными инициализаторами, и
 * порядок полей обязан совпадать с порядком объявления — таково требование языка.
 */
struct PanelDescriptor
{
    /**
     * \brief Идентификатор панели.
     *
     * Уникален среди **всех** панельных плагинов, а не только внутри своего: попадает в
     * настройки как выбранная панель и служит адресом для
     * spotty::IPanelHost::activatePanel(). Принято составлять из идентификатора плагина:
     * `"macros"`, `"plotter.plot"`.
     *
     * \warning Переименование сбрасывает пользователю выбранную панель и её настройки.
     */
    QString id;

    /// \brief Подпись и всплывающая подсказка, через tr().
    QString title;

    /**
     * \brief Кодовая точка значка из шрифта Material Design Icons.
     *
     * Имена доступны в spotty/ui/MdiCodepoints.h. Ноль — панель без значка; в рейке она
     * получит первую букву заголовка.
     */
    char32_t glyph = 0;

    PanelPlacement placement = PanelPlacement::Rail;

    /// \brief Порядок среди панелей того же размещения; совпадения разводятся по #id.
    int order = 100;

    PanelSide side = PanelSide::Below;

    /// \brief Желаемый размер полосы в пикселях по её главной оси.
    int preferredSize = 200;

    PanelAnchor anchor = PanelAnchor::Fill;

    /**
     * \brief Пропускать мышь насквозь к терминалу.
     *
     * Для слоёв, которые только показывают и ни на что не реагируют: иначе выделение
     * текста в терминале перестаёт работать там, где лежит слой.
     */
    bool mouseTransparent = false;

    /// \brief Показывать сразу. Скрытую панель пользователь включает сам.
    bool visibleByDefault = true;
};

/**
 * \struct PanelShortcut
 * \brief Сочетание клавиш, объявленное панелью.
 *
 * \note Панель не имеет права создавать QShortcut на главном окне сама: такие сочетания
 *       переживают её собственное уничтожение, а раздел настроек о них не узнает. Набор
 *       объявляется целиком через spotty::IPanelHost::setShortcuts(), и всем остальным
 *       занимается хост.
 */
struct PanelShortcut
{
    /// \brief Идентификатор внутри плагина; хост дополнит его до `"<pluginId>.<id>"`.
    QString id;

    /// \brief Название в разделе «Сочетания клавиш», через tr().
    QString title;

    QKeySequence defaultSequence;

    /**
     * \brief Показывать в разделе настроек и позволять переназначить.
     *
     * `false` для сочетаний, у которых уже есть своё место правки, — например, клавиш
     * макросов, задаваемых в самой таблице макросов. Два места правки одного и того же
     * неминуемо разошлись бы.
     */
    bool userConfigurable = true;
};

/**
 * \struct SearchOptions
 * \brief Уточнения к образцу поиска.
 */
struct SearchOptions
{
    bool regularExpression = false;
    bool caseSensitive = false;
    bool wholeWords = false;
};

/**
 * \struct TerminalLine
 * \brief Разобранная строка вывода.
 *
 * \note Отрезков оформления здесь нет намеренно. Раскраска — дело виджета терминала, а
 *       тип отрезка заморозил бы в ABI лишнюю структуру. Панели, читающей поток, нужен
 *       текст и время; кому нужны байты — есть #raw.
 */
struct TerminalLine
{
    QString text;                                ///< Текст без управляющих последовательностей.
    QByteArray raw;                              ///< Исходные байты строки.
    qint64 monotonicNs = 0;                      ///< Отметка чтения монотонными часами.
    QDateTime wallClock;                         ///< Время по системным часам.
    DataDirection direction = DataDirection::Rx;
    quint8 source = 0;                           ///< Номер транспорта для метки `A:` / `B:`.
    bool complete = false;                       ///< Строка завершена переводом строки.
};

/**
 * \struct TerminalGutterSettings
 * \brief Какие служебные поля терминал рисует слева от текста.
 *
 * Панели получают снимок параметров, а не TerminalView: виджет принадлежит приложению и
 * не входит в SDK. Это также позволяет журналу воспроизвести контекст строки без доступа
 * к приватной реализации терминала.
 */
struct TerminalGutterSettings
{
    bool showLineNumbers = false;
    bool showSource = false;
    bool showTimestamps = false;
    bool relativeTimestamps = false;
    bool showDirection = false;
    qint64 lineNumberOrigin = -1;
    QString timestampFormat = QStringLiteral("HH:mm:ss.zzz");
};

} // namespace spotty
