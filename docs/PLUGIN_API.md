# API плагинов Spotty

Полное описание SDK и пошаговое руководство по написанию плагина интерфейса.

**Версия API:** 2 (`SPOTTY_API_VERSION`)

---

## Содержание

- [Обзор](#обзор)
- [Из чего состоит SDK](#из-чего-состоит-sdk)
- [Правила, которые нельзя нарушать](#правила-которые-нельзя-нарушать)
- [Справочник: IInterfacePlugin](#справочник-iinterfaceplugin)
- [Справочник: IInterfaceChannel](#справочник-iinterfacechannel)
- [Справочник: InterfaceDescriptor](#справочник-interfacedescriptor)
- [Справочник: SettingsSchema](#справочник-settingsschema)
- [Справочник: ChannelState](#справочник-channelstate)
- [Пошагово: пишем плагин TCP](#пошагово-пишем-плагин-tcp)
- [Сборка и установка](#сборка-и-установка)
- [Отладка](#отладка)
- [Совместимость версий](#совместимость-версий)
- [Частые ошибки](#частые-ошибки)

---

## Обзор

Плагин Spotty добавляет поддержку одного транспорта: последовательный порт, TCP-сокет,
шина CAN, воспроизведение файла. Он отвечает на три вопроса:

1. **Какие устройства сейчас доступны?** → `IInterfacePlugin::enumerate()`
2. **Что у них можно настроить?** → `IInterfacePlugin::settingsSchema()`
3. **Как к ним подключиться?** → `IInterfacePlugin::createChannel()`

Всё остальное — разбор ANSI, разбиение потока на пакеты, метки времени, логирование,
макросы, поиск, интерфейс пользователя — делает ядро.

Минимальный работающий плагин: **пять методов и один вызов в CMake**.

---

## Из чего состоит SDK

Всё лежит в `include/spotty/api/`. Больше плагину знать ничего не нужно.

| Файл | Что содержит |
|---|---|
| `IInterfacePlugin.h` | Точка входа: перечисление устройств и создание каналов |
| `IInterfaceChannel.h` | Живое соединение: открыть, закрыть, писать, сигналы |
| `InterfaceDescriptor.h` | Описание одного найденного устройства |
| `SettingsSchema.h` | Декларативное описание настраиваемых параметров |
| `ChannelState.h` | Состояния жизненного цикла соединения |
| `ApiVersion.h` | Версия ABI и идентификатор интерфейса |
| `SpottyApiExport.h` | Макрос видимости символов |

---

## Правила, которые нельзя нарушать

Четыре пункта. Каждый нарушается легко, а последствия неочевидны.

### 1. Канал живёт в отдельном потоке

Ядро переносит канал в выделенный поток ввода-вывода (`moveToThread`) **до** вызова
`open()`.

- Не трогайте виджеты. Плагин вообще не линкуется с `Qt6::Widgets`.
- Не создавайте таймеры, сокеты и дескрипторы в конструкторе — только в `open()`. Объект
  с таймером, созданным в одном потоке и перенесённым в другой, работает не так, как
  ожидается.
- `Q_EMIT` из потока ввода-вывода безопасен: соединения очередные.

Причина требования: UART на 3 Мбод выдаёт `readyRead` достаточно часто, чтобы полностью
занять поток интерфейса. Встроить многопоточность в готовый плагин заметно дороже, чем
сразу под неё написать.

### 2. Идентификатор устройства должен переживать переподключение

Самая коварная ошибка во всём API.

```cpp
// ПЛОХО: на macOS и Linux узел /dev переназначается при каждом переподключении
descriptor.id = info.portName();

// ХОРОШО: опираемся на свойства, зашитые в железо
descriptor.id = QStringLiteral("uart:%1:%2:%3")
                    .arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0'))
                    .arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))
                    .arg(info.serialNumber());

// Запасной вариант — только когда свойств нет (встроенный UART, виртуальный порт)
if (!info.hasVendorIdentifier())
    descriptor.id = QStringLiteral("uart:%1").arg(info.portName());
```

Что сломается при плохом идентификаторе: `/dev/cu.usbserial-1420` после перевтыкания
становится `/dev/cu.usbserial-1430`, ядро считает устройство новым, сбрасывает скорость,
псевдоним и режим на умолчания, а автоматическое переоткрытие не срабатывает, потому что
вернувшееся устройство «не то, что пропало».

### 3. `enumerate()` вызывается раз в секунду в потоке интерфейса

Метод обязан быть дешёвым и неблокирующим. Если перечисление дорогое — обход сети, опрос
шины, — кэшируйте результат и обновляйте кэш из `hotplugNotifier()`.

### 4. Отметка времени ставится в момент чтения

```cpp
Q_EMIT dataReceived(chunk, m_clock.nsecsElapsed());   // именно здесь, не позже
```

Из неё вычисляются межбайтовые паузы для пакетизации и относительные метки времени в
терминале. Задержка в несколько миллисекунд ломает разбиение потока на кадры.

Часы обязаны быть **монотонными** (`QElapsedTimer`), а не системными: перевод часов или
синхронизация по NTP иначе дадут отрицательные интервалы.

---

## Справочник: IInterfacePlugin

### Обязательные методы

#### `QString pluginId() const`

Устойчивый идентификатор в нижнем регистре, пригодный для имени файла: `"uart"`, `"tcp"`.

> **Внимание.** Составляет первую половину сохраняемых идентификаторов устройств.
> Переименование обнуляет все сохранённые пользователем настройки.

Тем же идентификатором плагин выключается: раздел «Plugins» в настройках даёт флажок
напротив каждого найденного плагина, а снятый флажок кладёт `pluginId()` в список
`general/disabledPlugins`. Выключенный плагин при следующем запуске создаётся, но не
регистрируется — `enumerate()` у него не зовут, устройств в списке интерфейсов не
появляется. Отказом это не считается, в отчёте о загрузке его нет.

#### `QString displayName() const`

Название для пользователя, через `tr()`: `tr("Serial / UART")`.

#### `QList<InterfaceDescriptor> enumerate() const`

Устройства, видимые прямо сейчас. Поле `pluginId` можно не заполнять — реестр проставит
его сам, чем исключается целый класс ошибок.

#### `SettingsSchema settingsSchema() const`

Описание настраиваемых параметров. Диалог строит ядро, плагин виджетов не создаёт.

#### `IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor)`

Создаёт канал; владение переходит вызывающей стороне. Возвращает `nullptr`, если
дескриптор чужой.

### Необязательные методы

#### `QString settingsSummary(const QVariantMap &settings) const`

Однострочная выжимка для списка интерфейсов: `"115200 8-N-1"`. Пустая строка убирает
колонку.

Ядро не может составить её само: какие параметры существенны и как они читаются вместе —
знание транспорта.

```cpp
QString UartPlugin::settingsSummary(const QVariantMap &settings) const
{
    return QStringLiteral("%1 %2-%3-%4")
        .arg(settings.value("baudRate").toInt())
        .arg(settings.value("dataBits").toInt())
        .arg(settings.value("parity").toString())
        .arg(settings.value("stopBits").toString());
}
```

#### `QList<SettingsOption> liveOptions(const InterfaceDescriptor &, const QString &key, const QVariantMap &settings)`

Пункты для поля схемы с `live = true` — те, что становятся известны только после опроса.
Список последовательных портов известен целиком до открытия диалога, а узлы на шине CAN
отвечают на широковещательный запрос не мгновенно и не все сразу.

Ядро строит редактор **сразу**, по объявленным `options` (их может не быть вовсе), и
дальше, пока поле видно на экране, спрашивает плагин примерно раз в секунду, дополняя
список найденным. Окно поэтому открывается мгновенно, а выбранное пользователем — в том
числе набранное руками — переживает обновление.

```cpp
QList<SettingsOption> CliCanPlugin::liveOptions(const InterfaceDescriptor &descriptor,
                                                const QString &key,
                                                const QVariantMap &settings)
{
    if (key != QLatin1String("node"))
        return {};

    // Опрос заводится лениво, при первом вопросе: раньше он не нужен, а скорость шины
    // приходит здесь же, в settings.
    startScanIfNeeded(descriptor, settings);

    QList<SettingsOption> options;
    for (int node : scannedNodes())
        options.append({QString::number(node), node});
    return options;
}
```

> **Вызов — это и запрос, и продление.** Другого способа сообщить «опрос больше не нужен»
> у ядра нет: закрытый диалог просто перестаёт спрашивать. Поэтому опрос заводят лениво и
> гасят по молчанию — иначе шина останется занятой после того, как пользователь закрыл
> окно. Вызывается в потоке UI: возвращать нужно накопленное, а не ждать ответа устройств
> внутри вызова.
>
> **«Не ждать» касается и того, что зовёт `startScanIfNeeded()`, а не только самого
> `liveOptions()`.** Первая версия `clican` открывала шину прямо здесь — `CanBusPool::
> acquire()`, а внутри блокирующий `CAN_Initialize()` — и диалог зависал в момент выбора
> адаптера в списке. Открытие устройства обязано уйти в отдельный поток, а результат —
> вернуться через `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)`, как и у
> канала; сам `liveOptions()` в это время просто отдаёт то, что уже накоплено (пустой
> список, если открытие ещё не завершилось). См. `CliCanPlugin::beginScanOpen()`.

#### `int apiVersion() const`

Переопределять не нужно. `PluginManager` отказывается загружать плагин с несовпадающим
значением и объясняет причину пользователю.

#### `QObject *hotplugNotifier()`

Возвращает объект, испускающий `void devicesChanged()`, — тогда опрос раз в секунду
заменяется настоящими уведомлениями системы (udev, `WM_DEVICECHANGE`, IOKit). Возврат
`nullptr` оставляет опрос: это нормальный рабочий вариант.

Базового класса для уведомителя в SDK нет специально — реестр подключается к сигналу по
имени, поэтому подойдёт любой `QObject` с нужной сигнатурой.

> Объект обязан жить не меньше плагина: подписка делается один раз при запуске.

---

## Справочник: IInterfaceChannel

### Обязательные методы

| Метод | Назначение |
|---|---|
| `bool open(const QVariantMap &settings, QString *error)` | Открыть соединение |
| `void close()` | Закрыть; обязан быть безопасен при повторном вызове |
| `qint64 write(const QByteArray &data)` | Поставить в очередь на передачу; -1 при ошибке |
| `ChannelState state() const` | Текущее состояние |

`settings` уже приведены к схеме плагина: **каждый объявленный ключ гарантированно
присутствует**, проверять на наличие не нужно.

Текст в `error` увидит пользователь. Он должен объяснять, что произошло («Порт занят
другой программой»), а не воспроизводить код `errno`.

### Необязательные методы

| Метод | По умолчанию |
|---|---|
| `bool applySettings(const QVariantMap &)` | `false` → ядро закроет и откроет канал заново |
| `QVariantMap controlLines() const` | пустая карта → линий нет |
| `bool setControlLine(const QString &, bool)` | `false` → не поддерживается |
| `bool sendBreak(int ms)` | `false` → не поддерживается |

`applySettings()` стоит переопределить там, где транспорт умеет менять параметры на лету:
перезакрытие заметно пользователю — вывод прерывается, устройство может перезагрузиться от
перепада DTR.

### Сигналы

```cpp
void dataReceived(const QByteArray &data, qint64 monotonicNs);
void stateChanged(spotty::ChannelState state, const QString &detail);
void errorOccurred(const QString &message);
void controlLinesChanged();
```

`dataReceived` отдаёт байты **как есть**. Разбирать их на строки не нужно: правило
пакетизации выбирает пользователь, и ядро применит его само.

`errorOccurred` — для восстановимых ошибок (кадрирование, чётность, переполнение). Для
фатальной нужно дополнительно испустить `stateChanged(ChannelState::Error, ...)`: сам по
себе этот сигнал состояние не меняет.

---

## Справочник: InterfaceDescriptor

```cpp
struct InterfaceDescriptor
{
    QString pluginId;      // заполняется реестром, можно оставить пустым
    QString id;            // устойчивый идентификатор — см. правило 2
    QString systemName;    // "COM5", "/dev/ttyUSB0"
    QString description;   // "FT232R USB UART"
    QVariantMap extra;     // vendorId, productId, serialNumber, manufacturer...
    bool isValid() const;  // true, если задан id
};
```

`extra` показывается как есть во всплывающей подсказке списка интерфейсов и помогает
различить два одинаковых переходника. Ядро эти поля не интерпретирует.

Псевдоним, сохранённые настройки и время обнаружения принадлежат ядру и хранятся в
`interfaces.json` по ключу `id`. Плагину их знать не нужно и хранить он их не должен.

---

## Справочник: SettingsSchema

Плагин описывает настройки декларативно, а диалог строит ядро.

### Поле настройки

```cpp
SettingsField{
    .key = QStringLiteral("baudRate"),   // ключ в QVariantMap, не переводится
    .label = tr("Baud rate"),            // подпись
    .group = tr("Port"),                 // раздел диалога
    .type = SettingsField::Choice,       // Choice | Integer | Toggle | Text
    .defaultValue = 115200,              // единственное место, где задано умолчание
    .options = {{QStringLiteral("9600"), 9600},
                {QStringLiteral("115200"), 115200}},
    .live = false,                       // пункты приходят опросом (см. liveOptions())
    .editable = true,                    // разрешить своё значение вне списка
    .minimum = 0, .maximum = 0,          // границы для Integer
    .suffix = tr("bps"),                 // единица измерения справа
    .hint = tr("..."),                   // пояснение под редактором
}
```

Структура объявлена как агрегат C++20, поэтому назначенные инициализаторы работают.
**Порядок полей обязан совпадать с порядком объявления** — таково требование языка.

### Типы полей

| Тип | Редактор | Значимые поля |
|---|---|---|
| `Choice` | выпадающий список | `options`, `live`, `editable` |
| `Integer` | числовое поле | `minimum`, `maximum`, `suffix` |
| `Toggle` | флажок | — |
| `Text` | строка | — |

### Методы схемы

| Метод | Назначение |
|---|---|
| `add(SettingsField)` | Добавить поле; возвращает `*this` для цепочки |
| `defaults()` | Умолчания из полей |
| `normalized(const QVariantMap &)` | Привести сохранённое к схеме |
| `groups()` | Разделы в порядке объявления |
| `fieldsInGroup(const QString &)` | Поля одного раздела |
| `field(const QString &key)` | Найти поле по ключу |

`normalized()` — то, ради чего всё устроено именно так. Через него проходит всё чтение
настроек, поэтому конфигурация, записанная старой версией плагина, остаётся рабочей после
того, как плагин обрёл новый параметр или потерял старый. Отсутствующие ключи получают
умолчание, лишние отбрасываются.

> Отдельного метода `defaultSettings()` в `IInterfacePlugin` нет намеренно: умолчания
> объявлены в полях схемы, и второе место для того же факта неминуемо разошлось бы с
> первым.

---

## Справочник: ChannelState

```cpp
enum class ChannelState { Closed, Opening, Open, Unavailable, Error };
```

Разница между `Closed` и `Unavailable` несёт смысл, а не косметику:

- **`Closed`** — канал закрыл пользователь. Программа не должна открывать его сама: это
  было бы борьбой с явным намерением человека.
- **`Unavailable`** — устройство пропало из системы. Пользователь ничего не решал, порт
  был открыт и должен быть открыт снова, как только устройство вернётся.

Именно по `Unavailable` реестр понимает, что канал нужно переоткрыть автоматически.

```
                  open()
 Closed ───────────────────────────► Opening ──────► Open
    ▲                                   │              │
    │ close()                           │ ошибка       │ ошибка
    │                                   ▼              ▼
    └───────────────────────────────  Error ◄──────────┘

 Open ──── устройство исчезло ────► Unavailable
 Unavailable ── устройство вернулось ──► Opening (автоматически)
```

---

## Пошагово: пишем плагин TCP

Полный пример транспорта, совершенно непохожего на последовательный порт.

### Шаг 1. Каталог и метаданные

```
plugins/tcp/
├── CMakeLists.txt
├── tcp.json
├── TcpPlugin.h
├── TcpPlugin.cpp
├── TcpChannel.h
└── TcpChannel.cpp
```

`tcp.json` — метаданные для `Q_PLUGIN_METADATA`:

```json
{
    "id": "tcp",
    "name": "TCP client",
    "version": "1.0"
}
```

### Шаг 2. Класс плагина

```cpp
// TcpPlugin.h
#pragma once
#include <spotty/api/IInterfacePlugin.h>
#include <QObject>

namespace spotty {

class TcpPlugin : public QObject, public IInterfacePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_INTERFACE_PLUGIN_IID FILE "tcp.json")
    Q_INTERFACES(spotty::IInterfacePlugin)

public:
    QString pluginId() const override { return QStringLiteral("tcp"); }
    QString displayName() const override { return tr("TCP client"); }

    QList<InterfaceDescriptor> enumerate() const override;
    SettingsSchema settingsSchema() const override;
    QString settingsSummary(const QVariantMap &settings) const override;
    IInterfaceChannel *createChannel(const InterfaceDescriptor &descriptor) override;
};

} // namespace spotty
```

### Шаг 3. Перечисление устройств

У TCP нет физических устройств, поэтому «устройство» — это заданное пользователем
соединение. Отдаём одну постоянную запись:

```cpp
QList<InterfaceDescriptor> TcpPlugin::enumerate() const
{
    InterfaceDescriptor descriptor;
    descriptor.id = QStringLiteral("tcp:client");
    descriptor.systemName = QStringLiteral("tcp");
    descriptor.description = tr("TCP connection");
    return {descriptor};
}
```

Постоянный идентификатор здесь допустим именно потому, что запись не привязана к железу.
Для настоящих устройств см. правило 2.

### Шаг 4. Схема настроек

```cpp
SettingsSchema TcpPlugin::settingsSchema() const
{
    SettingsSchema schema;

    schema.add(SettingsField{
        .key = QStringLiteral("host"),
        .label = tr("Host"),
        .group = tr("Connection"),
        .type = SettingsField::Text,
        .defaultValue = QStringLiteral("127.0.0.1"),
    });

    schema.add(SettingsField{
        .key = QStringLiteral("port"),
        .label = tr("Port"),
        .group = tr("Connection"),
        .type = SettingsField::Integer,
        .defaultValue = 23,
        .minimum = 1,
        .maximum = 65535,
    });

    schema.add(SettingsField{
        .key = QStringLiteral("keepAlive"),
        .label = tr("TCP keep-alive"),
        .group = tr("Connection"),
        .type = SettingsField::Toggle,
        .defaultValue = true,
    });

    return schema;
}

QString TcpPlugin::settingsSummary(const QVariantMap &settings) const
{
    return QStringLiteral("%1:%2")
        .arg(settings.value(QStringLiteral("host")).toString())
        .arg(settings.value(QStringLiteral("port")).toInt());
}
```

### Шаг 5. Канал

```cpp
// TcpChannel.h
#pragma once
#include <spotty/api/IInterfaceChannel.h>
#include <QElapsedTimer>

class QTcpSocket;

namespace spotty {

class TcpChannel : public IInterfaceChannel
{
    Q_OBJECT
public:
    using IInterfaceChannel::IInterfaceChannel;

    bool open(const QVariantMap &settings, QString *error) override;
    void close() override;
    qint64 write(const QByteArray &data) override;
    ChannelState state() const override { return m_state; }

private:
    void setState(ChannelState state, const QString &detail = {});

    QTcpSocket *m_socket = nullptr;
    QElapsedTimer m_clock;
    ChannelState m_state = ChannelState::Closed;
};

} // namespace spotty
```

```cpp
// TcpChannel.cpp
bool TcpChannel::open(const QVariantMap &settings, QString *error)
{
    // Сокет создаётся здесь, а не в конструкторе: к этому моменту объект уже перенесён
    // в поток ввода-вывода, и сокет должен принадлежать именно ему.
    m_socket = new QTcpSocket(this);
    m_clock.start();

    connect(m_socket, &QTcpSocket::readyRead, this, [this] {
        // Отметка времени — в момент чтения, монотонными часами.
        Q_EMIT dataReceived(m_socket->readAll(), m_clock.nsecsElapsed());
    });

    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        // Разрыв соединения — не то же, что закрытие пользователем: Unavailable даст
        // ядру основание переоткрыть канал, когда узел снова ответит.
        setState(ChannelState::Unavailable, tr("Connection closed by peer"));
    });

    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] {
        Q_EMIT errorOccurred(m_socket->errorString());
    });

    setState(ChannelState::Opening);

    m_socket->connectToHost(settings.value(QStringLiteral("host")).toString(),
                            quint16(settings.value(QStringLiteral("port")).toInt()));

    if (!m_socket->waitForConnected(5000)) {
        if (error)
            *error = m_socket->errorString();   // текст увидит пользователь
        setState(ChannelState::Error, m_socket->errorString());
        return false;
    }

    setState(ChannelState::Open);
    return true;
}

void TcpChannel::close()
{
    if (m_socket) {
        m_socket->disconnectFromHost();
        delete m_socket;
        m_socket = nullptr;
    }
    setState(ChannelState::Closed);
}

qint64 TcpChannel::write(const QByteArray &data)
{
    return m_socket ? m_socket->write(data) : -1;
}

void TcpChannel::setState(ChannelState state, const QString &detail)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(m_state, detail);
}
```

### Шаг 6. Сборка

`plugins/tcp/CMakeLists.txt`:

```cmake
spotty_add_plugin(spotty-plugin-tcp
    CLASS_NAME TcpPlugin
    SOURCES
        TcpPlugin.cpp
        TcpPlugin.h
        TcpChannel.cpp
        TcpChannel.h
    LINK Qt6::Network
)
```

И одна строка в `plugins/CMakeLists.txt`:

```cmake
add_subdirectory(tcp)
```

Всё. Плагин соберётся и загружаемым модулем, и вкомпилированным в исполняемый файл — за
это отвечает `spotty_add_plugin()`, менять в исходниках ничего не нужно.

---

## Сборка и установка

### Два режима

| Режим | Когда | Как |
|---|---|---|
| Загружаемый модуль | по умолчанию, разработка | `.so` / `.dll` / `.dylib` в каталоге плагинов |
| Вкомпилированный | релизы, подпись под macOS | `-DSPOTTY_STATIC_PLUGINS=ON` |

Исходный код плагина в обоих случаях один и тот же.

### Куда попадает собранный плагин

| Система | Путь |
|---|---|
| macOS | `build/spotty.app/Contents/PlugIns/spotty/` |
| Linux, Windows | `build/plugins/` |

### Где приложение ищет плагины

В порядке приоритета:

1. Каталоги из `SPOTTY_PLUGIN_PATH` (разделитель `:`, в Windows `;`)
2. `<каталог конфигурации>/plugins` — плагины, установленные пользователем
3. Каталог плагинов внутри приложения
4. `<каталог приложения>/../lib/spotty/plugins`

При совпадении идентификаторов побеждает найденный раньше. Благодаря этому свежую сборку
можно положить в пользовательский каталог, и она перекроет штатную.

---

## Отладка

### Журнал загрузки

```bash
QT_LOGGING_RULES="spotty.plugins=true;spotty.registry=true" ./build/spotty.app/Contents/MacOS/spotty
```

```
spotty.plugins: loaded "tcp" from "/path/to/libspotty-plugin-tcp.dylib"
spotty.plugins: loaded 2 plugin(s), 0 rejected
spotty.registry: found "tcp:client"
```

### Отчёт в окне

Пока терминал не реализован, центральная область показывает список загруженных плагинов,
отклонённые файлы с причинами и обойденные каталоги. Отклонённый плагин виден там же —
не приходится искать по журналам.

### Запуск из своего каталога

```bash
SPOTTY_PLUGIN_PATH=/path/to/my/plugins ./build/spotty.app/Contents/MacOS/spotty
```

---

## Совместимость версий

Два независимых рубежа защиты.

**`SPOTTY_INTERFACE_PLUGIN_IID`** проверяет сам Qt ещё до создания объекта. При
несовпадении `qobject_cast` вернёт `nullptr`, и плагин просто не будет распознан.

**`SPOTTY_API_VERSION`** проверяет `PluginManager` и показывает пользователю внятную
причину:

> Built against API version 1, this build expects 2.

Версию нужно увеличивать при любом изменении, ломающем уже собранные плагины: смена
сигнатуры виртуального метода, удаление метода, изменение раскладки передаваемых структур.

> **Не очевидно:** добавление нового виртуального метода **в конец** класса, даже с
> реализацией по умолчанию, тоже ломает ABI — смещения в таблице виртуальных функций
> сдвигаются. Версию нужно увеличивать и в этом случае.

### История версий

| Версия | Что изменилось |
|---|---|
| 2 | `liveOptions()` и `SettingsField::live` — пункты списка, известные только после опроса. Сломано и то и другое сразу: новый виртуальный метод сдвигает vtable, новое поле структуры — раскладку `SettingsSchema` |
| 1 | Первый выпуск |

Правка `SettingsSchema` дотянулась и до панельного SDK — схему возвращают и панели, —
поэтому вместе с основной поднята `SPOTTY_UI_API_VERSION`. Ассерт в `UiApiVersion.h`
существует ровно для того, чтобы это не прошло незамеченным.

---

## Частые ошибки

### Плагин не загружается, в отчёте — сообщение Qt

Почти всегда это несовпадение версии Qt или компилятора между плагином и приложением.
Загружаемые плагины Qt требуют совпадения того и другого — это свойство Qt, а не Spotty.
Обходной путь: собрать со `SPOTTY_STATIC_PLUGINS=ON`.

### Сигналы канала не доходят до приложения

Проверьте, что плагин линкуется со `Spotty::Api`, а не тянет заголовки SDK напрямую через
`target_include_directories`. Во втором случае у плагина появится собственная копия
`IInterfaceChannel::staticMetaObject`, а Qt сопоставляет сигналы, сравнивая **указатели**
на `QMetaObject`. Ошибки сборки при этом нет, предупреждения тоже — просто ничего не
работает.

Правильно всегда: `spotty_add_plugin()` подключает `Spotty::Api` сам.

### Настройки устройства сбрасываются после переподключения

`InterfaceDescriptor::id` выведен из системного имени. См. правило 2.

### Настройки пропали после обновления плагина

Ключ в `SettingsField::key` переименован. Ключи — часть формата хранения, они не
переводятся и не меняются. При необходимости переименования нужен код переноса старых
значений.

### Интерфейс подтормаживает на высокой скорости

`enumerate()` делает что-то тяжёлое. Он вызывается раз в секунду в потоке интерфейса —
кэшируйте результат и обновляйте кэш из `hotplugNotifier()`.

### Пакетизация по таймауту работает неправильно

Отметка времени в `dataReceived()` ставится не в момент чтения либо взята из системных, а
не монотонных часов. См. правило 4.
