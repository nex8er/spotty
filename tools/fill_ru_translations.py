#!/usr/bin/env python3
"""Заполняет русские переводы в resources/i18n/spotty_ru.ts.

Порядок работы с переводами:

    cmake --build build --target update_translations   # lupdate соберёт строки из кода
    python3 tools/fill_ru_translations.py              # проставит known-переводы
    cmake --build build                                # lrelease соберёт .qm в ресурсы

Скрипт трогает только пустые и помеченные `unfinished` переводы, поэтому правки,
внесённые вручную или в Qt Linguist, он не затирает.

С `--force` перезаписываются и готовые переводы — это нужно, когда формулировку в
словаре исправили: без ключа правка осталась бы только в скрипте и до интерфейса не
дошла.

Строки, которых нет в словаре, остаются непереведёнными и печатаются в конце — это
единственный способ заметить, что в интерфейсе появился новый текст.
"""

import re
import sys
from pathlib import Path

TS_FILE = Path(__file__).resolve().parent.parent / "resources" / "i18n" / "spotty_ru.ts"

#: Переводы по исходной строке. Ключи — ровно то, что стоит в tr() в коде.
TRANSLATIONS = {
    # --- Общие элементы интерфейса ---
    "&File": "&Файл",
    "&View": "&Вид",
    "&Help": "&Справка",
    "&Interface": "&Интерфейс",
    "&Quit": "В&ыход",
    "&Settings...": "&Настройки...",
    "&About Spotty": "&О программе",
    "About Spotty": "О программе",
    "&Theme": "&Тема",
    "&Dark": "&Тёмная",
    "&Light": "&Светлая",
    "Dark": "Тёмная",
    "Light": "Светлая",
    "System": "Системный",
    "Settings": "Настройки",
    "Settings - %1": "Настройки — %1",
    "General": "Общие",
    "Terminal": "Терминал",
    "Send": "Отправка",
    "Logging": "Логирование",
    "Data": "Данные",
    "Interfaces": "Интерфейсы",
    "Shortcuts": "Горячие клавиши",
    "Language": "Язык",
    "Theme": "Тема",
    "Interface": "Интерфейс",
    "Device": "Устройство",
    "Name": "Имя",
    "Alias": "Псевдоним",
    "Format": "Формат",
    "Termination": "Терминация",
    "Shortcut": "Сочетание клавиш",
    "Pattern": "Выражение",
    "Colour": "Цвет",
    "Directory": "Каталог",
    "File name": "Имя файла",
    "Length": "Длина",
    "Preview": "Предпросмотр",
    "Start": "Пуск",
    "Stop": "Стоп",
    "Copy": "Копировать",
    "Select All": "Выделить всё",
    "Browse...": "Обзор...",
    "Repeat": "Повтор",
    "Mode": "Режим",
    "None": "Нет",
    "Text": "Текст",
    "Hex": "Hex",
    "Base64": "Base64",

    # --- Строка интерфейса и состояния ---
    "Not selected": "Не выбрано",
    "Open": "Открыт",
    "Opening": "Открывается",
    "Closed": "Закрыт",
    "Unavailable": "Недоступен",
    "Error": "Ошибка",
    "unavailable": "недоступен",
    "Open the interface": "Открыть интерфейс",
    "Close the interface": "Закрыть интерфейс",
    "Interface settings": "Настройки интерфейса",
    "Address": "Адрес",
    "VID:PID": "VID:PID",
    "Hide from the interface list": "Скрыть из списка интерфейсов",
    "&Open / Close": "&Открыть / закрыть",
    "Open / close interface": "Открыть или закрыть интерфейс",
    "Toggle &DTR": "Переключить &DTR",
    "Toggle &RTS": "Переключить &RTS",
    "Send &Break": "Послать &BREAK",
    "Control lines": "Линии управления",
    "Control lines: uppercase means asserted":
        "Линии управления: заглавные — линия поднята",
    "RX %1  TX %2": "Принято %1  Отправлено %2",
    "  ·  %1/s": "  ·  %1/с",

    # --- Терминал ---
    "Show data as a hexadecimal dump": "Показать данные шестнадцатеричным дампом",
    "Show timestamps": "Показывать метки времени",
    "Show transmit and receive marks": "Показывать метки приёма и передачи",
    "Clear the terminal": "Очистить терминал",
    "Clear terminal": "Очистить терминал",
    "C&lear terminal": "О&чистить терминал",
    "Follow output": "Следовать за выводом",
    "Scroll to Bottom": "Прокрутить вниз",
    "&Hexadecimal dump": "&Шестнадцатеричный дамп",
    "&Timestamps": "&Метки времени",
    "Toggle hexadecimal dump": "Переключить шестнадцатеричный дамп",
    "Toggle timestamps": "Переключить метки времени",
    "Show time relative to the previous line":
        "Показывать время относительно предыдущей строки",
    "Timestamp format": "Формат метки времени",
    "Qt date/time format, for example HH:mm:ss.zzz":
        "Формат даты и времени Qt, например HH:mm:ss.zzz",
    "Font": "Шрифт",
    "Font size": "Размер шрифта",
    "Buffer size": "Размер буфера",
    "Bytes per hex row": "Байт в строке дампа",
    "Received text encoding": "Кодировка принимаемого текста",
    "Echo sent data into the terminal": "Отражать отправленное в терминале",
    "ANSI colours": "Цвета ANSI",
    "ANSI colour": "Цвет ANSI",
    "Reset to theme colours": "Вернуть цвета темы",
    "Colours will follow the theme again.": "Цвета снова будут следовать теме.",
    "Black": "Чёрный",
    "Red": "Красный",
    "Green": "Зелёный",
    "Yellow": "Жёлтый",
    "Blue": "Синий",
    "Magenta": "Пурпурный",
    "Cyan": "Голубой",
    "White": "Белый",
    "Bright black": "Яркий чёрный",
    "Bright red": "Яркий красный",
    "Bright green": "Яркий зелёный",
    "Bright yellow": "Яркий жёлтый",
    "Bright blue": "Яркий синий",
    "Bright magenta": "Яркий пурпурный",
    "Bright cyan": "Яркий голубой",
    "Bright white": "Яркий белый",

    # --- Строка отправки ---
    "Data to send": "Данные для отправки",
    "Open an interface to send data": "Откройте интерфейс, чтобы отправлять данные",
    "How the entered text is interpreted": "Как трактовать введённый текст",
    "Appended to every message": "Дописывается к каждой посылке",
    "Default format": "Формат по умолчанию",
    "Default termination": "Терминация по умолчанию",
    "History size": "Размер истории",
    "History is kept in a plain text file next to the configuration and survives restarts.":
        "История хранится текстовым файлом рядом с настройками и переживает перезапуск.",
    "Focus &send bar": "Фокус в &строку отправки",
    "Focus send bar": "Фокус в строку отправки",
    "The interface is not open.": "Интерфейс не открыт.",
    "No interface selected.": "Интерфейс не выбран.",

    # --- Кодирование данных ---
    "Not valid Base64.": "Некорректная запись Base64.",
    '"%1" is not a hexadecimal digit.': '«%1» не шестнадцатеричная цифра.',
    "Odd number of hexadecimal digits - one byte is two digits.":
        "Нечётное число шестнадцатеричных цифр — в байте их две.",

    # --- Макросы ---
    "Macros": "Макросы",
    "Command": "Команда",
    "Key": "Клавиша",
    "Send now": "Отправить",
    "Duplicate": "Дублировать",
    "Delete": "Удалить",
    "%1 · %2\nDouble-click to edit, right-click for options":
        "%1 · %2\nДвойной щелчок — правка, правая кнопка — действия",
    "Import macros": "Импорт макросов",
    "Export macros": "Экспорт макросов",
    "Import macros...": "Импортировать...",
    "Export macros...": "Экспортировать...",
    "Macro files (*.json)": "Файлы макросов (*.json)",
    "Could not read %1.": "Не удалось прочитать %1.",
    "Could not write %1.": "Не удалось записать %1.",
    "Exported to %1": "Экспортировано в %1",
    "There is nothing to export.": "Экспортировать нечего.",
    "Start repeating": "Начать повтор",
    "Stop repeating": "Остановить повтор",
    "Add macro": "Добавить макрос",
    "Edit macro": "Изменить макрос",
    "Delete macro": "Удалить макрос",
    "New macro": "Новый макрос",
    "New preset": "Новый набор",
    "Delete preset": "Удалить набор",
    'Delete preset "%1" and its file?': "Удалить набор «%1» вместе с файлом?",
    "Could not create a preset with that name.":
        "Не удалось создать набор с таким именем.",
    "Macro preset; each preset is a separate file":
        "Набор макросов; каждый набор — отдельный файл",
    "Shown on the macro list": "Показывается в списке макросов",
    'Macro "%1": %2': "Макрос «%1»: %2",
    "Macro shortcuts are assigned in the Macros panel, on each macro.":
        "Горячие клавиши макросов задаются в панели макросов, у каждого макроса.",
    "actual: %1 ms": "фактически: %1 мс",

    # --- Логирование ---
    # Коротко: подпись стоит в узкой боковой панели и в полном виде не помещается.
    # Подробное объяснение живёт во всплывающей подсказке.
    "Strip ANSI escape sequences": "Убирать коды ANSI",
    "Colour codes make the file hard to read outside a terminal and break searching through it.":
        "Цветовые коды мешают читать файл вне терминала и ломают поиск по нему.",
    "Include sent data": "Записывать отправленное",
    "Start recording": "Начать запись",
    "Stop recording": "Остановить запись",
    "Start / stop logging": "Начать или остановить запись",
    "Start / stop &recording": "Начать или остановить &запись",
    "Start recording when the interface opens":
        "Начинать запись при открытии интерфейса",
    "Recent logs": "Последние логи",
    "Recording to %1": "Запись в %1",
    "Click to view in the terminal. Drag out or press Ctrl+C to copy the file itself.":
        "Щелчок открывает файл в терминале. Перетаскивание или Ctrl+C копируют сам файл.",
    "Select an interface before recording.": "Выберите интерфейс перед началом записи.",
    "Cannot create the log directory: %1": "Не удалось создать каталог логов: %1",
    "Cannot open the log file: %1": "Не удалось открыть файл лога: %1",
    "Log write failed: %1": "Ошибка записи лога: %1",
    "Log directory": "Каталог логов",
    "Placeholders: {alias}, {interface}, {date}, {time}":
        "Подстановки: {alias}, {interface}, {date}, {time}",
    "Viewing log: %1": "Просмотр лога: %1",
    "Back to live output": "К живому выводу",
    "Cannot open %1: %2": "Не удалось открыть %1: %2",
    "%1\n%2, %3": "%1\n%2, %3",

    # --- Поиск ---
    "Search": "Поиск",
    "Find in output": "Найти в выводе",
    "&Find...": "&Найти...",
    "Focus search": "Фокус в поле поиска",
    "Next match": "Следующее совпадение",
    "Previous match": "Предыдущее совпадение",
    "Regular expression": "Регулярное выражение",
    "Case sensitive": "Учитывать регистр",
    "Whole words": "Слова целиком",
    "Show only matching lines": "Только совпавшие строки",
    "Hides everything that does not match, instead of just highlighting it.":
        "Скрывает всё несовпавшее, а не просто подсвечивает совпадения.",
    "Highlight rules": "Правила подсветки",
    "Highlight colour": "Цвет подсветки",
    "Add rule": "Добавить правило",
    "Delete rule": "Удалить правило",
    "Double-click to change": "Двойной щелчок меняет цвет",

    # --- Генератор ---
    "Generator": "Генератор",
    "Packet counter": "Счётчик посылок",
    "Random bytes": "Случайные байты",
    "Fixed byte": "Постоянный байт",
    "Ramp 00..FF": "Пила 00..FF",
    "ASCII text": "Текст ASCII",
    "Byte value": "Значение байта",
    "Stream interval": "Период потока",
    "Send once": "Отправить один раз",
    "Stream": "Поток",
    "To send bar": "В строку отправки",
    "Put the generated data into the send bar without sending":
        "Положить порождённые данные в строку отправки, не отправляя",

    # --- Пакетизация ---
    "Split incoming data by": "Разбивать принимаемое",
    "Stream (split on line breaks)": "Потоком (по переводам строк)",
    "Inter-byte timeout": "По межбайтовой паузе",
    "Delimiter": "По разделителю",
    "Fixed length": "По фиксированной длине",
    "Gap": "Пауза",
    "Message length": "Длина сообщения",
    "Hexadecimal, for example 0A or 0D0A":
        "Шестнадцатеричная запись, например 0A или 0D0A",
    "A binary stream without line breaks collapses into one endless line. "
    "Splitting by an inter-byte gap is how most binary protocols over UART frame "
    "their messages.":
        "Двоичный поток без переводов строк слипается в одну бесконечную строку. "
        "Разбиение по межбайтовой паузе — то, как размечает сообщения большинство "
        "двоичных протоколов поверх UART.",

    # --- Общие настройки ---
    "Takes effect after restarting Spotty.": "Вступит в силу после перезапуска Spotty.",
    "Open the remembered interface on startup":
        "Открывать запомненный интерфейс при запуске",
    "Opening a port asserts DTR, which resets many boards, and takes the port away "
    "from any other program using it.":
        "Открытие порта поднимает DTR, а у многих плат это сброс; порт при этом "
        "перехватывается у другой программы, которая им пользуется.",
    "Only one running copy of Spotty": "Только одна работающая копия Spotty",
    "Starting Spotty again raises the existing window instead of opening a second one.":
        "Повторный запуск покажет уже открытое окно вместо второго.",
    "The language and single-instance settings take effect after Spotty is restarted.":
        "Язык и режим единственного экземпляра вступят в силу после перезапуска Spotty.",
    "<b>Spotty %1</b><br>Modular terminal port monitor.<br><br>Configuration: %2":
        "<b>Spotty %1</b><br>Модульный терминал-монитор портов.<br><br>Настройки: %2",

    # --- UART ---
    "Serial / UART": "Последовательный порт / UART",
    "Port": "Порт",
    "Baud rate": "Скорость",
    "Data bits": "Бит данных",
    "Parity": "Чётность",
    "Stop bits": "Стоп-биты",
    "Flow control": "Управление потоком",
    "Even": "Чётная",
    "Odd": "Нечётная",
    "Mark": "Единица",
    "Space": "Ноль",
    "Hardware (RTS/CTS)": "Аппаратное (RTS/CTS)",
    "Software (XON/XOFF)": "Программное (XON/XOFF)",
    "Assert DTR on open": "Поднимать DTR при открытии",
    "Assert RTS on open": "Поднимать RTS при открытии",
    "On many boards DTR is wired to reset - clear it to avoid rebooting the device "
    "when the port opens.":
        "У многих плат DTR заведён на сброс — снимите флажок, чтобы открытие порта "
        "не перезагружало устройство.",
    "Invalid baud rate.": "Недопустимая скорость.",
    "The port does not support %1 baud.": "Порт не поддерживает скорость %1.",
    "bps": "бод",

    # --- Loopback ---
    "Loopback": "Петля",
    "Behaviour": "Поведение",
    "Echo what is sent": "Возвращать отправленное",
    "Emit generated data": "Выдавать порождённые данные",
    "Silent": "Молчать",
    "Echo delay": "Задержка эха",
    "Simulates a device that takes time to answer.":
        "Изображает устройство, которому нужно время на ответ.",
    "Emit interval": "Период выдачи",
    "Include ANSI colour codes": "Добавлять цветовые коды ANSI",
    "Virtual channel": "Виртуальный канал",
    "Virtual echo channel": "Виртуальный канал-эхо",
    "Virtual data source": "Виртуальный источник данных",

    # --- J-Link RTT ---
    "J-Link RTT": "J-Link RTT",
    "J-Link": "J-Link",
    "RTT%1": "RTT%1",
    "Connection": "Подключение",
    "Target interface": "Целевой интерфейс",
    "SWD": "SWD",
    "JTAG": "JTAG",
    "Speed": "Скорость",
    "kHz": "кГц",
    "Target device": "Целевое устройство",
    "Exact SEGGER device name, e.g. \"NRF52832_XXAA\" — start typing to search. Required "
    "to connect.":
        "Точное имя устройства SEGGER, например «NRF52832_XXAA» — начните вводить для "
        "поиска. Обязательно для подключения.",
    "Exact SEGGER device name, e.g. \"NRF52832_XXAA\". Required to connect. JLinkDevices.xml "
    "was not found, so there are no suggestions — install SEGGER J-Link software for "
    "autocomplete.":
        "Точное имя устройства SEGGER, например «NRF52832_XXAA». Обязательно для "
        "подключения. JLinkDevices.xml не найден, подсказок не будет — установите SEGGER "
        "J-Link software для автодополнения.",
    "%1 %2 kHz": "%1 %2 кГц",
    "Could not open J-Link S/N %1: %2": "Не удалось открыть J-Link S/N %1: %2",
    "Could not connect to the target: %1": "Не удалось подключиться к таргету: %1",
    "Could not start RTT: %1": "Не удалось запустить RTT: %1",
    "RTT control block not found on the target yet — check that RTT is initialised in "
    "firmware and the target is running.":
        "Управляющий блок RTT ещё не найден на таргете — проверьте, что RTT "
        "инициализирован в прошивке и таргет запущен.",

    # --- Сообщения сессии ---
    "--- %1 opened ---": "--- %1 открыт ---",
    "--- closed ---": "--- закрыт ---",
    "--- device disconnected ---": "--- устройство отключено ---",
    "--- device is back, reopening ---": "--- устройство вернулось, открываю заново ---",
    "Device disconnected": "Устройство отключено",
    "Device is not present": "Устройства нет в системе",
    'Interface "%1" is not known.': "Интерфейс «%1» неизвестен.",
    'Plugin "%1" is not loaded.': "Плагин «%1» не загружен.",
    'Plugin "%1" could not create a channel.': "Плагин «%1» не смог создать канал.",
    "Could not open the interface.": "Не удалось открыть интерфейс.",
    "Could not reopen the interface.": "Не удалось открыть интерфейс заново.",

    # --- Загрузка плагинов ---
    "Not a Spotty interface plugin.": "Это не плагин интерфейса Spotty.",
    "Built against API version %1, this build expects %2.":
        "Собран для версии API %1, эта сборка ожидает %2.",
    "Plugin reports an empty id.": "Плагин сообщает пустой идентификатор.",
    'Another plugin already provides id "%1".':
        "Идентификатор «%1» уже занят другим плагином.",

    # --- Единицы измерения ---
    "B": "Б",
    "KiB": "КиБ",
    "MiB": "МиБ",
    "GiB": "ГиБ",
    "ms": "мс",
    " ms": " мс",
    " bytes": " байт",
    " lines": " строк",
    " entries": " записей",
    "%1 ms": "%1 мс",
    "%1 s": "%1 с",
    "just now": "только что",
}

#: Переводы форм множественного числа: source -> (одна, несколько, много).
#: Русский требует трёх форм, и Qt ожидает их именно в этом порядке.
PLURALS = {
    "%n s ago": ("%n секунду назад", "%n секунды назад", "%n секунд назад"),
    "%n min ago": ("%n минуту назад", "%n минуты назад", "%n минут назад"),
    "%n h ago": ("%n час назад", "%n часа назад", "%n часов назад"),
    "%n d ago": ("%n день назад", "%n дня назад", "%n дней назад"),
    "%n line(s)": ("%n строка", "%n строки", "%n строк"),
    "%n error(s)": ("%n ошибка", "%n ошибки", "%n ошибок"),
    "  ·  %n error(s)": ("  ·  %n ошибка", "  ·  %n ошибки", "  ·  %n ошибок"),
    "%n packet(s) sent": ("отправлена %n посылка", "отправлено %n посылки",
                          "отправлено %n посылок"),
    "%n macro(s) imported": ("импортирован %n макрос", "импортировано %n макроса",
                             "импортировано %n макросов"),
    "Could not send %n byte(s).": ("Не удалось отправить %n байт.",
                                   "Не удалось отправить %n байта.",
                                   "Не удалось отправить %n байт."),
}


def escape(text: str) -> str:
    """Экранирует символы, значимые для XML."""
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def unescape(text: str) -> str:
    """Обратное преобразование: в словаре ключи записаны обычным текстом."""
    return (text.replace("&quot;", '"').replace("&amp;", "&")
                .replace("&lt;", "<").replace("&gt;", ">"))


def translate_message(block: str, missing: list[str], force: bool) -> tuple[str, bool]:
    """Заполняет перевод в одном блоке <message>.

    Возвращает изменённый блок и признак того, что перевод был проставлен.
    """
    # Уже переведённое не трогаем: правки из Qt Linguist должны переживать запуск.
    # Исключение — режим --force, когда словарь исправили и нужно раскатать правку.
    if not force and 'type="unfinished"' not in block:
        return block, False

    source_match = re.search(r"<source>(.*?)</source>", block, re.S)
    if not source_match:
        return block, False

    source = unescape(source_match.group(1))

    if "<numerusform>" in block:
        forms = PLURALS.get(source)
        if not forms:
            missing.append(source)
            return block, False
        body = "".join(f"<numerusform>{escape(form)}</numerusform>" for form in forms)
    else:
        translation = TRANSLATIONS.get(source)
        if translation is None:
            missing.append(source)
            return block, False
        body = escape(translation)

    updated = re.sub(r"<translation[^>]*>.*?</translation>",
                     f"<translation>{body}</translation>", block, flags=re.S)
    return updated, True


def main() -> int:
    # --force перезаписывает и уже готовые переводы. Нужен, когда формулировку в словаре
    # исправили: без него правка осталась бы только в скрипте и до интерфейса не дошла.
    force = "--force" in sys.argv[1:]

    text = TS_FILE.read_text(encoding="utf-8")
    missing: list[str] = []
    filled = 0

    def handle(match: re.Match) -> str:
        nonlocal filled
        block, changed = translate_message(match.group(0), missing, force)
        filled += int(changed)
        return block

    text = re.sub(r"<message[^>]*>.*?</message>", handle, text, flags=re.S)
    TS_FILE.write_text(text, encoding="utf-8")

    print(f"переведено: {filled}")
    if missing:
        print(f"без перевода: {len(set(missing))}")
        for source in sorted(set(missing)):
            print(f"  {source!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
