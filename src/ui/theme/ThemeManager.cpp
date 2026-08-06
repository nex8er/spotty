/**
 * \file ThemeManager.cpp
 * \brief Реализация spotty::ThemeManager.
 */
#include "ThemeManager.h"

#include "PlatformChrome.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QFrame>
#include <QLayout>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QWidget>

#include <algorithm>

namespace spotty {

namespace {

/// \brief Первый созданный экземпляр; возвращается из ThemeManager::instance().
ThemeManager *g_instance = nullptr;

/**
 * \brief Цвета тёмной темы.
 *
 * Оформление аскетичное и монохромное: нейтральные серые, никаких градиентов, единственный
 * приглушённый синий акцент — на контурах и на единственной главной кнопке экрана.
 *
 * \note Все пары «текст на фоне» проверены по WCAG 2.2 на худшем из фонов, где эта роль
 *       действительно встречается. Самый узкий случай — textMuted поверх panel: прежний
 *       `#81868c` давал там 4.03:1 при требуемых 4.5:1, и подсказки с метками времени
 *       формально не читались.
 */
ThemeColors darkColors()
{
    ThemeColors c;
    c.window = QColor(0x1e, 0x20, 0x22);
    c.panel = QColor(0x26, 0x28, 0x2b);
    c.base = QColor(0x17, 0x19, 0x1a);
    c.border = QColor(0x35, 0x38, 0x3c);
    c.controlBorder = QColor(0x6e, 0x74, 0x7b);    // 3.1:1 на panel
    c.text = QColor(0xd8, 0xda, 0xdc);
    c.textMuted = QColor(0x91, 0x98, 0xa0);        // 5.1:1 на panel
    c.accent = QColor(0x4f, 0x9d, 0xd9);           // 5.0:1 на panel
    c.accentText = QColor(0xff, 0xff, 0xff);
    c.selection = QColor(0x2f, 0x4c, 0x62);
    c.accentSolid = QColor(0x2f, 0x6e, 0xa3);      // 5.4:1 с белой подписью
    c.accentSolidHover = QColor(0x3a, 0x7f, 0xb8);
    c.accentSolidPressed = QColor(0x27, 0x5c, 0x8a);
    c.hover = QColor(0x2f, 0x32, 0x36);
    c.pressed = QColor(0x38, 0x3c, 0x41);
    c.rxText = QColor(0xd8, 0xda, 0xdc);
    c.txText = QColor(0x7f, 0xa8, 0x5c);
    c.errorText = QColor(0xd2, 0x6b, 0x6b);
    c.okText = QColor(0x6f, 0xa8, 0x6f);
    return c;
}

/// \brief Цвета светлой темы. Те же роли, тот же принцип, та же проверка контраста.
ThemeColors lightColors()
{
    ThemeColors c;
    c.window = QColor(0xf4, 0xf5, 0xf6);
    c.panel = QColor(0xea, 0xec, 0xee);
    c.base = QColor(0xff, 0xff, 0xff);
    c.border = QColor(0xd0, 0xd3, 0xd7);
    c.controlBorder = QColor(0x7d, 0x83, 0x8a);    // 3.2:1 на panel
    c.text = QColor(0x1d, 0x1f, 0x21);
    c.textMuted = QColor(0x5f, 0x65, 0x6b);        // 5.0:1 на panel
    c.accent = QColor(0x1a, 0x60, 0x99);           // 5.6:1 на panel
    c.accentText = QColor(0xff, 0xff, 0xff);
    c.selection = QColor(0xbe, 0xd8, 0xef);
    c.accentSolid = QColor(0x1a, 0x60, 0x99);      // 6.6:1 с белой подписью
    c.accentSolidHover = QColor(0x22, 0x6f, 0xae);
    c.accentSolidPressed = QColor(0x14, 0x50, 0x7f);
    c.hover = QColor(0xdd, 0xe0, 0xe4);
    c.pressed = QColor(0xd0, 0xd4, 0xd9);
    c.rxText = QColor(0x1d, 0x1f, 0x21);
    c.txText = QColor(0x3d, 0x6b, 0x22);
    c.errorText = QColor(0xb3, 0x33, 0x33);
    c.okText = QColor(0x2e, 0x7d, 0x32);
    return c;
}

/**
 * \brief Собрать размеры оформления под текущую операционную систему.
 *
 * \par Откуда взяты числа
 *
 * Windows — Fluent 2: корпус текста 14 px (10 pt при 96 dpi), скругление 4 px, высота поля
 * 32 px (28 в плотных списках). macOS — Human Interface Guidelines: системный шрифт 13 pt,
 * скругление 5–6 px, элементы ниже и с большими промежутками между блоками. Прочие системы
 * получают вариант Windows, но с системным кеглем: в Linux он задаётся окружением рабочего
 * стола, и переписывать его своим значением значило бы игнорировать настройку доступности.
 */
ThemeMetrics buildMetrics()
{
    ThemeMetrics m;

#if defined(Q_OS_WIN)
    // Системный кегль Windows — 9 pt, но современные приложения набирают текст 14 px
    // (10 pt при 96 dpi). Девять точек в окне с плотной таблицей и терминалом читаются
    // мелко, поэтому здесь единственное место, где системное значение переопределяется.
    m.fontPointSize = 10;
    m.radius = 4;
    m.cardRadius = 6;
    m.padV = 5;
    m.gap = 8;
    m.scrollBarWidth = 12;
#elif defined(Q_OS_MACOS)
    // Кегль не трогаем: системный шрифт macOS уже 13 pt, и его меняет настройка
    // «Размер текста» в системных параметрах.
    m.fontPointSize = 0;
    m.radius = 5;
    m.cardRadius = 8;
    m.padV = 4;
    // macOS расставляет блоки просторнее: те же 8 px там читаются как слипшиеся.
    m.gap = 10;
    m.scrollBarWidth = 11;
#else
    m.fontPointSize = 0;
    m.radius = 4;
    m.cardRadius = 6;
    m.padV = 5;
    m.gap = 8;
    m.scrollBarWidth = 12;
#endif

    // Захват разделителя шире рисуемой линии: линию в один пиксель невозможно поймать
    // курсором, а промах по ней ничего не делает и выглядит как неработающий разделитель.
    m.splitterHandle = 6;

    // Кнопки счётчика делят поле пополам по вертикали, но живут **внутри** рамки
    // (subcontrol-origin: padding), поэтому считаются от высоты содержимого с отступами,
    // без двух пикселей самой рамки. Учесть их здесь значило бы сделать кнопки выше
    // отведённого места: подсветка при наведении вылезла бы на рамку и закрасила её
    // вместе со скруглением угла.
    m.spinButtonHeight = (m.controlHeight + 2 * m.padV) / 2;

    // Кегли заголовка и подписи считаются от действующего, а не задаются числом: если
    // система (или пользователь) укрупнила шрифт ради читаемости, вся шкала должна
    // поехать за ней, иначе укрупнится ровно половина интерфейса.
    int base = m.fontPointSize;
    if (base <= 0) {
        base = QFontDatabase::systemFont(QFontDatabase::GeneralFont).pointSize();
        if (base <= 0)
            base = 10;
    }
    m.titlePointSize = base + 1;
    m.smallPointSize = qMax(base - 1, 8);

    return m;
}

/**
 * \brief Построить QPalette из набора цветов.
 *
 * Палитра нужна помимо таблицы стилей: её читают виджеты, рисующие себя сами, и стандартные
 * диалоги, до которых QSS не дотягивается.
 */
QPalette buildPalette(const ThemeColors &c)
{
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.panel);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.panel);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, c.accentText);
    p.setColor(QPalette::ToolTipBase, c.panel);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.textMuted);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.border);

    p.setColor(QPalette::Disabled, QPalette::Text, c.textMuted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.textMuted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.textMuted);
    return p;
}

/**
 * \brief Подставить в таблицу стилей цвета и размеры вместо плейсхолдеров `@имя`.
 * \param sheet Исходный текст QSS.
 * \param c Цвета текущей темы.
 * \param m Размеры для текущей операционной системы.
 * \return Готовую таблицу стилей.
 *
 * Размеры подставляются вместе с единицей измерения (`6px`, `10pt`), чтобы в самом QSS не
 * приходилось писать `@radiuspx` — такое склеивание разборщик молча не понял бы.
 */
QString resolveStylesheet(QString sheet, const ThemeColors &c, const ThemeMetrics &m)
{
    const auto px = [](int value) { return QStringLiteral("%1px").arg(value); };
    const auto pt = [](int value) { return QStringLiteral("%1pt").arg(value); };

    QList<QPair<QString, QString>> substitutions = {
        {QStringLiteral("@window"), c.window.name(QColor::HexRgb)},
        {QStringLiteral("@panel"), c.panel.name(QColor::HexRgb)},
        {QStringLiteral("@base"), c.base.name(QColor::HexRgb)},
        {QStringLiteral("@borderControl"), c.controlBorder.name(QColor::HexRgb)},
        {QStringLiteral("@border"), c.border.name(QColor::HexRgb)},
        {QStringLiteral("@text"), c.text.name(QColor::HexRgb)},
        {QStringLiteral("@textMuted"), c.textMuted.name(QColor::HexRgb)},
        {QStringLiteral("@accentSolidPressed"), c.accentSolidPressed.name(QColor::HexRgb)},
        {QStringLiteral("@accentSolidHover"), c.accentSolidHover.name(QColor::HexRgb)},
        {QStringLiteral("@accentSolid"), c.accentSolid.name(QColor::HexRgb)},
        {QStringLiteral("@accentText"), c.accentText.name(QColor::HexRgb)},
        {QStringLiteral("@accent"), c.accent.name(QColor::HexRgb)},
        {QStringLiteral("@selection"), c.selection.name(QColor::HexRgb)},
        {QStringLiteral("@hover"), c.hover.name(QColor::HexRgb)},
        {QStringLiteral("@pressed"), c.pressed.name(QColor::HexRgb)},
        {QStringLiteral("@errorText"), c.errorText.name(QColor::HexRgb)},
        {QStringLiteral("@okText"), c.okText.name(QColor::HexRgb)},

        {QStringLiteral("@radius"), px(m.radius)},
        {QStringLiteral("@cardRadius"), px(m.cardRadius)},
        {QStringLiteral("@controlHeight"), px(m.controlHeight)},
        {QStringLiteral("@buttonMinWidth"), px(m.buttonMinWidth)},
        {QStringLiteral("@padV"), px(m.padV)},
        {QStringLiteral("@padH"), px(m.padH)},
        {QStringLiteral("@gap"), px(m.gap)},
        {QStringLiteral("@scrollBar"), px(m.scrollBarWidth)},
        {QStringLiteral("@spinButton"), px(m.spinButtonHeight)},
        {QStringLiteral("@fontTitle"), pt(m.titlePointSize)},
        {QStringLiteral("@fontSmall"), pt(m.smallPointSize)},
    };

    // Сначала длинные: подстановка @text раньше @textMuted превратила бы второй в
    // «#d8dadcMuted», и разборщик CSS молча отбросил бы такое правило. То же и с
    // @accent/@accentSolid/@accentSolidHover, и с @radius/@cardRadius.
    std::sort(substitutions.begin(), substitutions.end(),
              [](const auto &lhs, const auto &rhs) {
                  return lhs.first.size() > rhs.first.size();
              });

    for (const auto &[placeholder, value] : substitutions)
        sheet.replace(placeholder, value);

    // Оставшийся @имя — опечатка в таблице стилей. Без проверки это был бы цвет, который
    // просто ничего не делает. Комментарии исключаем: в самом QSS написано про
    // «@placeholders».
    static const QRegularExpression comments(QStringLiteral("/\\*.*?\\*/"),
                                             QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression leftover(QStringLiteral("@[A-Za-z][A-Za-z0-9]*"));

    const QRegularExpressionMatch match = leftover.match(QString(sheet).remove(comments));
    if (match.hasMatch())
        qWarning("spotty: unknown stylesheet placeholder %s", qPrintable(match.captured()));

    return sheet;
}

} // namespace

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    if (!g_instance)
        g_instance = this;

    // Фильтр ставит только первый экземпляр: второй красил бы те же окна теми же цветами
    // и просто удваивал бы работу на каждом событии показа.
    if (g_instance == this && qApp)
        qApp->installEventFilter(this);
}

ThemeManager *ThemeManager::instance()
{
    return g_instance;
}

const ThemeMetrics &ThemeManager::metrics()
{
    static const ThemeMetrics m = buildMetrics();
    return m;
}

void ThemeManager::setTheme(Theme theme)
{
    m_theme = theme;
    apply();
    Q_EMIT themeChanged(m_theme);
}

void ThemeManager::apply()
{
    m_colors = m_theme == Theme::Dark ? darkColors() : lightColors();

    // Проверка на QApplication, а не QCoreApplication: класс должен оставаться безвредным
    // в консольном окружении, например в модульных тестах ядра.
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return;

    const ThemeMetrics &m = metrics();

    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(buildPalette(m_colors));

    // Шрифт — после стиля: QApplication::setStyle заново «полирует» виджеты и возвращает
    // им шрифт стиля, затирая установленный раньше.
    //
    // Берём именно системный шрифт, а не имя семейства строкой: так интерфейс говорит
    // шрифтом системы (Segoe UI, San Francisco, шрифт рабочего стола в Linux), а не
    // случайно похожим. Кегль переопределяем только там, где системное значение заметно
    // расходится с принятым в современных приложениях этой системы, — см. buildMetrics().
    QFont uiFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (m.fontPointSize > 0)
        uiFont.setPointSize(m.fontPointSize);
    QApplication::setFont(uiFont);

    QFile file(QStringLiteral(":/themes/spotty.qss"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        app->setStyleSheet(resolveStylesheet(QString::fromUtf8(file.readAll()), m_colors, m));

    applyWindowChrome();
}

void ThemeManager::applyWindowChrome()
{
    const bool dark = m_theme == Theme::Dark;
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        // Только уже показанные: у скрытого окна системного дескриптора может ещё не быть,
        // а winId() создал бы его раньше времени. Скрытые получат своё в eventFilter().
        if (window->isVisible())
            PlatformChrome::applyWindowAppearance(window, dark);
    }
}

namespace {

/**
 * \brief Оформить окно раскрытого списка под текущую тему.
 * \param window Показываемое окно верхнего уровня; не список — не делает ничего.
 * \param colors Цвета текущей темы.
 *
 * \par Почему это в коде, а не в таблице стилей
 *
 * Раскрытый список — это два виджета: окно-контейнер `QComboBoxPrivateContainer` (оно же
 * QFrame) и вложенный в него вид. Таблица стилей достаёт только до вида: имя приватного
 * класса селектором не выбирается — проверено, правило с таким именем не срабатывает.
 *
 * \par Откуда взялись полосы над списком и под ним
 *
 * Как только у QComboBox появляется собственная рамка, `QStyleSheetStyle` начинает
 * отвечать на `SH_ComboBox_Popup` утвердительно, и всплывающий список переключается в
 * «менюшный» режим. В нём `QComboBoxPrivate::showPopup()` раздвигает окно на
 * `PM_MenuVMargin` сверху и снизу — те самые несколько пикселей, которые оставались
 * незакрашенными и читались как чужая тёмная рамка. Убрать этот отступ нечем: он берётся
 * из стиля до того, как окно вообще появится.
 *
 * Поэтому отступ не убирается, а становится полем внутри рамки: фон и рамку берёт на себя
 * сам контейнер, а вид внутри остаётся без рамки (см. `QComboBox QAbstractItemView` в
 * таблице стилей). Получается обычное меню с полями, а не список с полосами.
 *
 * \note Делается при каждом показе, а не однажды: контейнер перечитывает настройки стиля
 *       при каждой его смене, то есть и при смене темы.
 */
void applyComboPopupLook(QWidget *window, const ThemeColors &colors)
{
    auto *frame = qobject_cast<QFrame *>(window);
    if (!frame || !qobject_cast<QComboBox *>(frame->parentWidget()))
        return;

    // Фон и цвет рамки — палитрой, а не таблицей стилей: таблица, назначенная виджету без
    // селектора, распространилась бы и на вложенный вид, обведя рамкой ещё и его.
    QPalette palette = frame->palette();
    palette.setColor(QPalette::Window, colors.base);
    palette.setColor(QPalette::WindowText, colors.controlBorder);
    frame->setPalette(palette);
    frame->setAutoFillBackground(true);

    frame->setFrameShape(QFrame::Box);
    frame->setFrameShadow(QFrame::Plain);
    frame->setLineWidth(1);

    // Полосы прокрутки списка (`QComboBoxPrivateScroller`) — тоже дети контейнера, и фон
    // они закрашивают сами, по правилам меню: получается полоска цвета панели поперёк
    // списка. Палитра родителя их не догоняет, поэтому цвет ставится каждому.
    const auto children = frame->findChildren<QWidget *>(Qt::FindDirectChildrenOnly);
    for (QWidget *child : children) {
        if (qobject_cast<QAbstractItemView *>(child))
            continue;
        child->setPalette(palette);
        child->setAutoFillBackground(true);
    }
}

} // namespace

bool ThemeManager::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Show:
        if (auto *widget = qobject_cast<QWidget *>(watched); widget && widget->isWindow()) {
            // Только окна с рамкой: у всплывающего списка и подсказки заголовка нет, а
            // вызов ниже дёргает SetWindowPos и заставил бы их мигать при каждом показе.
            const Qt::WindowType type = widget->windowType();
            if (type == Qt::Window || type == Qt::Dialog)
                PlatformChrome::applyWindowAppearance(widget, m_theme == Theme::Dark);

            applyComboPopupLook(widget, m_colors);
        }
        break;

    case QEvent::ToolTipChange:
        // Подсказка у кнопки без надписи — единственное словесное описание того, что она
        // делает. Пока оно живёт только в подсказке, до чтеца с экрана оно не доходит:
        // тот читает accessibleName, а у кнопки со значком он пуст.
        if (auto *button = qobject_cast<QAbstractButton *>(watched);
            button && button->text().isEmpty() && button->accessibleName().isEmpty()) {
            button->setAccessibleName(button->toolTip());
        }
        break;

    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

QString ThemeManager::themeToString(Theme theme)
{
    return theme == Theme::Light ? QStringLiteral("light") : QStringLiteral("dark");
}

ThemeManager::Theme ThemeManager::themeFromString(const QString &name, Theme fallback)
{
    if (name.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0)
        return Theme::Dark;
    if (name.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0)
        return Theme::Light;
    return fallback;
}

} // namespace spotty
