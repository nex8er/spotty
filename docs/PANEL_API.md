# API панельных плагинов Spotty

Плагины, которые обрабатывают данные и показывают собственный интерфейс.

**Версия API:** 4 (`SPOTTY_UI_API_VERSION`)

Про плагины **транспортов** — отдельный документ: [PLUGIN_API.md](PLUGIN_API.md).

---

## Содержание

- [Чем этот вид отличается от транспорта](#чем-этот-вид-отличается-от-транспорта)
- [Из чего состоит SDK](#из-чего-состоит-sdk)
- [Правила, которые нельзя нарушать](#правила-которые-нельзя-нарушать)
- [Справочник: IPanelPlugin](#справочник-ipanelplugin)
- [Справочник: PanelDescriptor](#справочник-paneldescriptor)
- [Справочник: IPanelHost](#справочник-ipanelhost)
- [Справочник: PanelWidget](#справочник-panelwidget)
- [Справочник: IDataFilter](#справочник-idatafilter)
- [Пошагово: пишем плагин](#пошагово-пишем-плагин)
- [Сборка и установка](#сборка-и-установка)
- [Совместимость версий](#совместимость-версий)
- [Частые ошибки](#частые-ошибки)

---

## Чем этот вид отличается от транспорта

Транспорт отвечает на вопрос «откуда байты». Панельный плагин — на вопросы «что с ними
делать» и «как это показать».

| | Транспорт | Панель |
|---|---|---|
| Интерфейс | `IInterfacePlugin` | `IPanelPlugin` |
| Библиотека | `Spotty::Api` | `Spotty::UiApi` (включает первую) |
| `Qt6::Widgets` | запрещён | обязателен |
| Создаёт виджеты | нет | да, в этом его работа |
| Поток | канал в своём потоке ввода-вывода | всё в потоке интерфейса |
| `KIND` в CMake | `INTERFACE` (умолчание) | `PANEL` |
| Версия ABI | `SPOTTY_API_VERSION` | `SPOTTY_UI_API_VERSION` |

Один класс может реализовать оба интерфейса — тогда у обеих ролей должен быть одинаковый
`pluginId()`, и каталог данных у них общий.

Готовые примеры в дереве: `plugins/generator` (проще некуда), `plugins/search`,
`plugins/logging` (схема настроек и чтение потока), `plugins/macros` (таблица, свой
делегат, горячие клавиши), `plugins/plotter` (две панели: рейка и полоса вместо терминала),
`plugins/jsontree` (дерево из потока: логика в SDK и проверяется без виджетов, показ —
делегатом, перерисовка через троттлинг), `plugins/filesend` (длительная операция с
прогрессом и отменой).

---

## Из чего состоит SDK

| Файл | Что содержит |
|---|---|
| `spotty/ui/IPanelPlugin.h` | Точка входа: какие панели, какой фильтр, какая схема настроек |
| `spotty/ui/IPanelHost.h` | Приложение с точки зрения панели |
| `spotty/ui/PanelWidget.h` | Необязательная основа виджета панели |
| `spotty/ui/PanelDescriptor.h` | Описание панели, сочетания клавиш, строка терминала |
| `spotty/ui/MdiCodepoints.h` | Кодовые точки значков Material Design Icons |
| `spotty/ui/UiApiVersion.h` | Версия ABI и IID |
| `spotty/api/IDataFilter.h` | Звено цепочки преобразования потока |
| `spotty/data/*` | Готовая логика: `DataCodec`, `MacroStore`, `LogWriter`, `DataGenerator`, `HighlightRules`, `Formatting`, `FileUtils` |

Заголовки `spotty/api/*` доступны тоже: `SettingsSchema`, `ChannelState`, `DataDirection`.

---

## Правила, которые нельзя нарушать

### 1. Всё в потоке интерфейса

В отличие от канала, панель живёт в главном потоке. Долгая работа в обработчике сигнала
подвешивает окно. `dataReceived()` на 3 Мбод приходит сотни раз в секунду.

### 2. `window()` — только как родитель диалога

```cpp
// ХОРОШО: модальный диалог должен знать своё окно
QFileDialog::getOpenFileName(window(), tr("Send file"), ...);

// ПЛОХО: сочетание переживёт саму панель, а раздел настроек о нём не узнает
new QShortcut(QKeySequence("Ctrl+K"), window());
```

Сочетания объявляются через `IPanelHost::setShortcuts()`. Хост создаёт действия, снимает
их вместе с панелью и показывает в разделе «Сочетания клавиш» те, у которых
`userConfigurable == true`.

### 3. Настройки — только через хост

`IPanelHost::value()` и `setValue()` подставляют префикс `plugins/<pluginId>/` сами.
Ключ без префикса залезть в чужие настройки не может.

### 4. Идентификатор панели уникален глобально

`PanelDescriptor::id` попадает в настройки как выбранная панель и служит адресом для
`activatePanel()`. Он уникален среди **всех** панельных плагинов, а не внутри своего.
Принято составлять из идентификатора плагина: `"plotter"`, `"plotter.plot"`.

### 5. Идентификатор плагина — часть путей

Из `pluginId()` складываются каталог данных (`<конфигурация>/<pluginId>/`) и пространство
настроек. Переименование обнуляет всё, что пользователь настроил. Имя `plugins` занято.

Им же плагин выключается: флажок в разделе «Plugins» настроек кладёт `pluginId()` в список
`general/disabledPlugins`, и при следующем запуске реестр такой плагин пропускает —
`panels()` у него не спрашивают, ни одной панели не строится. Отказом это не считается.

---

## Справочник: IPanelPlugin

### Обязательные методы

```cpp
QString pluginId() const;     // "plotter"
QString displayName() const;  // tr("Plotter")
```

Всё. Плагин без панелей законен: звено цепочки преобразования может ничего не показывать.

### Необязательные методы

| Метод | По умолчанию |
|---|---|
| `QList<PanelDescriptor> panels() const` | пусто — плагин ничего не показывает |
| `QWidget *createPanel(panelId, host, parent)` | `nullptr` |
| `IDataFilter *dataFilter()` | `nullptr` — поток не трогается |
| `int filterOrder() const` | 500; меньше — раньше в приёмной цепочке |
| `SettingsSchema settingsSchema() const` | пусто — страницы в диалоге настроек нет |
| `int uiApiVersion() const` | переопределять не нужно |

Метода `apiVersion()` здесь намеренно нет: класс, реализующий обе роли, получил бы два
одноимённых чисто виртуальных метода и неоднозначный вызов.

---

## Справочник: PanelDescriptor

Агрегат C++20 — заполняется назначенными инициализаторами, порядок полей обязан совпадать
с порядком объявления.

```cpp
PanelDescriptor{
    .id = QStringLiteral("plotter.plot"),
    .title = tr("Plotter"),
    .placement = PanelPlacement::Splitter,
    .order = 500,
    .side = PanelSide::Below,
    .preferredSize = 320,
    .visibleByDefault = false,
}
```

### Три места на экране

| `placement` | Куда попадает | Когда уместно |
|---|---|---|
| `Rail` | страница в боковой рейке значков | настройки, таблицы, всё, что смотрят по требованию |
| `Splitter` | своя полоса над или под терминалом | то, что должно быть видно постоянно и ничего не закрывать |
| `Overlay` | слой поверх области вывода | то, что читается вместе с текстом: индикатор, подсказка |

`Splitter` дополнительно смотрит на `side` и `preferredSize`, `Overlay` — на `anchor` и
`mouseTransparent`.

**`Splitter` со снятым `visibleByDefault` — это полоса вместо терминала.** Такая панель не
показывается сама, зато даёт пункт в переключателе режима области вывода, и её собственные
`QAction` становятся кнопками в панели управления, когда она выбрана. Так сделан плоттер.
С поднятым `visibleByDefault` та же панель становится постоянной полосой над или под
терминалом — и ни пункта в переключателе, ни кнопок у неё уже нет.

**Слой относится к живому выводу.** При просмотре файла лога он прячется: график,
построенный по потоку, поверх чужого файла показывал бы не то, что означает.

**`mouseTransparent` нужен слою, который только показывает.** Без него терминал теряет
выделение текста на всей площади слоя.

---

## Справочник: IPanelHost

Один экземпляр на плагин, живёт дольше всех его панелей.

### Отправка

```cpp
void send(const QByteArray &data);
void composeInSendBar(const QString &text, DataCodec::Format format);
```

> **Не очевидно.** `send()` не блокирует: байты кладутся в очередь потока ввода-вывода.
> Мегабайт одним вызовом вырастит очередь на весь объём, и окно перестанет отвечать —
> без единой ошибки. Отправляя много, шлите порцию, ждите её в `dataLogged()` с
> `DataDirection::Tx`, потом следующую. Так делает `plugins/filesend`.

### Состояние интерфейса

`channelState()`, `interfaceId()`, `interfaceName()`, `interfaceAlias()`.

### Свои настройки и свои файлы

```cpp
QVariant value(const QString &key, const QVariant &fallback = {}) const;
void setValue(const QString &key, const QVariant &value);
QString dataDir() const;       // <конфигурация>/<pluginId>/, создаётся сам
QString documentsDir() const;  // для файлов, которые откроют другой программой
```

### Терминал

Запись: `appendToTerminal()`, `injectReceived()`, `clearTerminal()`, `showDocument()`.
Показ: `setHighlightRules()`, `setSearchPattern()`, `setFilterEnabled()`, `findNext()`,
`findPrevious()`, `matchCount()`, `currentMatch()`, `selectedText()`.
Чтение: `firstLineNumber()`, `nextLineNumber()`, `line(number, TerminalLine *)`.

> Нумерация строк сквозная и никогда не сбрасывается, но буфер подрезается спереди —
> `line()` вернёт `false` для вытесненной строки. Читать нужно от `firstLineNumber()`, а
> не от нуля.

### Оформление

`color(ColorRole)`, `metric(Metric)`, `isDarkTheme()`, `icon(glyph, size)`,
`mutedIcon(glyph, size)`.

Обычные виджеты панели оформляются общей таблицей стилей сами — селекторы `#panelTitle`,
`#hintLabel`, `#card` действуют и на них. Цвет спрашивают только для собственной
отрисовки.

> Цветов серий в теме нет и не будет: «цвет второго ряда графика» — не роль интерфейса.
> Плагин выбирает свои, а у темы спрашивает только `isDarkTheme()`.

### Сигналы

```cpp
void dataLogged(const QByteArray &data, spotty::DataDirection direction);
void dataReceived(const QByteArray &data, qint64 monotonicNs);
void terminalLinesAppended(qint64 firstLineNumber, qint64 count);
void channelStateChanged(spotty::ChannelState state);
void themeChanged();
void matchCountChanged(int currentMatch, int totalMatches);
void shortcutActivated(const QString &shortcutId);  // "<pluginId>.<id>"
void settingsReset();
void aboutToClose();
```

`dataLogged` и `dataReceived` отдают уровень провода — **до** цепочки преобразования.

---

## Справочник: PanelWidget

Необязательная основа. Даёт заголовок, поля раскладки и отклики на события хоста:

```cpp
class MyPanel : public PanelWidget
{
public:
    explicit MyPanel(IPanelHost *panelHost, QWidget *parent = nullptr)
        : PanelWidget(panelHost, parent)
    {
        setPanelTitle(tr("My panel"));
        content()->addWidget(...);
    }

protected:
    void themeChanged() override;                       // пересобрать значки
    void channelStateChanged(ChannelState) override;    // разрешить/запретить отправку
    void settingsReset() override;                      // перечитать своё
    void aboutToClose() override;                       // дописать файлы
};
```

> **Параметр конструктора называйте `panelHost`, а не `host`.** Иначе внутри тела
> конструктора он перекроет метод `host()`, и компилятор пожалуется на «вызов объекта, не
> являющегося функцией», указав при этом на лямбду, а не на причину.

---

## Справочник: IDataFilter

```cpp
QByteArray filterIncoming(const QByteArray &data, qint64 monotonicNs);
QByteArray filterOutgoing(const QByteArray &data);   // по умолчанию поток не трогается
```

Пустой возврат проглатывает порцию целиком. В приёмном направлении это значит «в терминал
ничего не пойдёт», в передающем — «не отправлять» (не ошибка).

Приёмная цепочка идёт по возрастанию `filterOrder()`, передающая — в обратную сторону, как
в стеке протоколов.

**Отметку времени звено не задаёт.** Байты, придержанные от прошлой порции и выпущенные
сейчас, получают время текущей — то есть время последнего байта кадра. Это то, что нужно и
относительным меткам, и разбиению по межбайтовой паузе.

---

## Пошагово: пишем плагин

### Шаг 1. Каталог

```
plugins/mypanel/
├── CMakeLists.txt
├── mypanel.json
├── MyPanelPlugin.h / .cpp
└── MyPanel.h / .cpp
```

`mypanel.json`:

```json
{
    "id": "mypanel",
    "name": "My panel",
    "version": "1.0"
}
```

### Шаг 2. Класс плагина

```cpp
#include <spotty/ui/IPanelPlugin.h>

namespace spotty {

class MyPanelPlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_PANEL_PLUGIN_IID FILE "mypanel.json")
    Q_INTERFACES(spotty::IPanelPlugin)

public:
    QString pluginId() const override { return QStringLiteral("mypanel"); }
    QString displayName() const override { return tr("My panel"); }

    QList<PanelDescriptor> panels() const override
    {
        return {PanelDescriptor{
            .id = QStringLiteral("mypanel"),
            .title = tr("My panel"),
            .glyph = mdi::Puzzle,
            .placement = PanelPlacement::Rail,
            .order = 700,
        }};
    }

    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override
    {
        if (panelId != QLatin1String("mypanel"))
            return nullptr;
        return new MyPanel(host, parent);
    }
};

} // namespace spotty
```

### Шаг 3. Сборка

`plugins/mypanel/CMakeLists.txt`:

```cmake
spotty_add_plugin(spotty-plugin-mypanel
    CLASS_NAME MyPanelPlugin
    KIND PANEL
    SOURCES
        MyPanelPlugin.cpp
        MyPanelPlugin.h
        MyPanel.cpp
        MyPanel.h
)
```

И строка в `plugins/CMakeLists.txt`:

```cmake
add_subdirectory(mypanel)
```

`KIND PANEL` — единственное отличие от транспорта. Оно добавляет `Spotty::UiApi`, а с ним
`Qt6::Widgets`.

### Шаг 4. Нужен свой значок

Впишите его имя в `ICONS` в `tools/gen_mdi.py` и запустите скрипт: он перегенерирует
`include/spotty/ui/MdiCodepoints.h`. Шрифт встроен целиком, но заголовок содержит только
используемые константы, а не таблицу на семь тысяч записей.

---

## Сборка и установка

Всё как у транспортов: загружаемый модуль по умолчанию, вкомпилированный при
`-DSPOTTY_STATIC_PLUGINS=ON`, те же каталоги поиска и та же переменная
`SPOTTY_PLUGIN_PATH`. Подробности — в [PLUGIN_API.md](PLUGIN_API.md#сборка-и-установка).

Каталоги обходятся **один раз** на оба вида плагинов: `PluginManager` складывает всё
созданное, `PanelPluginRegistry` разбирает панельную роль, и только потом
`PluginManager::finishLoading()` заносит в отчёт то, чью роль не признал никто.

---

## Совместимость версий

Два независимых рубежа, как и у транспортов: `SPOTTY_PANEL_PLUGIN_IID` проверяет Qt,
`SPOTTY_UI_API_VERSION` — реестр, с внятным сообщением.

Версия панельного API **отдельная** от `SPOTTY_API_VERSION`. Правка панельного контракта
не должна заставлять пересобирать UART, а `PluginManager` — отвергать его с причиной, к
которой транспорт непричастен. Связаны они `static_assert` в `UiApiVersion.h`: поднять
основную версию, не заметив панельную, не получится.

> Добавление виртуального метода **в конец** класса ломает ABI и здесь тоже.

### История версий

| Версия | Что изменилось |
|---|---|
| 4 | `IPanelHost::selectedText()` — чтение текущего выделения терминала. Вставлен не в конец, а между `currentMatch()` и разделом чтения строк: сдвигает vtable-смещения ещё сильнее, чем добавление в конец, поэтому версию поднимать обязательно даже при, казалось бы, «маленькой» правке |
| 3 | Дотянулась правка `SettingsField::live` в основном API — панельные плагины тоже возвращают `SettingsSchema` |
| 2 | Первый выпуск с текущей раскладкой `IPanelHost` |

---

## Частые ошибки

### Панель загрузилась, но не получает данных

Почти наверняка `spotty-api` и `spotty-ui-api` собраны разного вида (одна разделяемой,
другая статической), и у плагина своя копия метаобъектов. Проверка стоит в
`src/uiapi/CMakeLists.txt`; обе библиотеки порождаются из `SPOTTY_STATIC_PLUGINS`.

### Компилятор жалуется на «вызов объекта, не являющегося функцией»

Параметр конструктора назван `host` и перекрыл метод `host()`. Переименуйте в `panelHost`.

### Сочетание клавиш срабатывает после закрытия панели

Оно создано через `QShortcut` на `window()`. Объявляйте набор через
`IPanelHost::setShortcuts()`.

### Настройки пропали после переименования плагина

`pluginId()` входит в пути настроек и в имя каталога данных. Он не переименовывается.

### Окно подвисает при отправке большого файла

`send()` вызван одним куском. Нужно обратное давление — см. предупреждение в разделе
[Отправка](#отправка).

### Слой не виден, вместо него сплошной прямоугольник

Слою не хватает правила `#panelOverlay { background: transparent; border: none; }` — он
унаследовал вид `#card`. Правило уже есть в штатной таблице стилей; если панель задаёт
своё оформление, фон нужно погасить явно.
