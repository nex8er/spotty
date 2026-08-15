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
    "Undo": "Отменить",
    "Redo": "Повторить",
    "Cut": "Вырезать",
    "Paste": "Вставить",
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
    "%1/s": "%1/с",
    "  ·  %1": "  ·  %1",

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
    "Send to": "Отправить в",
    "Interface A": "Интерфейс A",
    "Interface B": "Интерфейс B",
    "Both": "Оба",
    "The interface is not open.": "Интерфейс не открыт.",
    "No interface selected.": "Интерфейс не выбран.",
    "Fill in the required settings to open this interface.":
        "Заполните обязательные настройки, чтобы открыть этот интерфейс.",

    # --- Подсказки на месте пустого вывода терминала ---
    "Choose an interface above to see its output here":
        "Выберите интерфейс наверху, чтобы увидеть здесь его вывод",
    "Interface is open — waiting for data": "Интерфейс открыт — ожидание данных",
    "Opening the interface…": "Открытие интерфейса…",
    "The interface could not be opened": "Интерфейс открыть не удалось",
    "This log file is empty": "Этот файл лога пуст",
    "No lines match the filter": "Ни одна строка не подходит под фильтр",

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
    "Use the context menu to view in the terminal. Drag out or press Ctrl+C to copy the file itself.":
        "Открывайте файл в терминале через контекстное меню. Перетаскивание или Ctrl+C "
        "копируют сам файл.",
    "Copy file": "Скопировать файл",
    "Delete selected": "Удалить выбранные",
    "Delete logs": "Удаление логов",
    "Could not delete: %1": "Не удалось удалить: %1",
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
    "For example: ^(WARN|ERROR).*[0-9]+$": "Например: ^(WARN|ERROR).*[0-9]+$",
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
    "Reset": "Сброс",
    "Erases all settings, remembered interfaces and the send history. Cannot be undone.":
        "Стирает все настройки, запомненные интерфейсы и историю отправки. Действие "
        "необратимо.",
    "Reset everything to defaults…": "Сбросить всё к умолчаниям…",
    "Reset to defaults": "Сброс к умолчаниям",
    "This erases all settings, remembered interfaces and the send history, and cannot be "
    "undone.\n\nContinue?":
        "Это сотрёт все настройки, запомненные интерфейсы и историю отправки — действие "
        "необратимо.\n\nПродолжить?",
    "Settings, interfaces and history have been reset to defaults.":
        "Настройки, интерфейсы и история сброшены к умолчаниям.",
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

    # --- Loopback ---
    "Loopback": "Петля",
    "Behaviour": "Поведение",
    "Echo what is sent": "Возвращать отправленное",
    "Emit generated data": "Выдавать порождённые данные",
    "Silent": "Молчать",
    "As the device implies": "По устройству",
    "loopback0 echoes what is sent, loopback1 emits generated lines.":
        "loopback0 возвращает отправленное, loopback1 выдаёт сгенерированные строки.",
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
    "Exact SEGGER device name, e.g. \"NRF52832_XXAA\". Required to connect. The J-Link "
    "device database was not found, so there are no suggestions — install SEGGER J-Link "
    "software for autocomplete.":
        "Точное имя устройства SEGGER, например «NRF52832_XXAA». Обязательно для "
        "подключения. База устройств J-Link не найдена, подсказок не будет — установите "
        "SEGGER J-Link software для автодополнения.",
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
    # --- Панель терминала и режимы вывода ---
    "What the output area shows": "Что показано в области вывода",
    "One interface": "Один интерфейс",
    "Two interfaces": "Два интерфейса",
    "Show line numbers": "Показывать номера строк",
    "Line numbers": "Номера строк",
    "Count from Here": "Считать отсюда",
    "Hide telemetry lines: values separated by the delimiter set in Settings. "
    "Right-click to change the delimiter":
        "Скрывать телеметрию: числа через разделитель, заданный в настройках. "
        "Разделитель можно сменить по правому клику",
    "Hide unreadable characters: control codes and invalid encoding. "
    "Right-click to choose how":
        "Скрывать нечитаемые символы: управляющие коды и битую кодировку. "
        "Способ показа — по правому клику",
    "Show as dots": "Точками",
    "Hide": "Скрывать",
    "Hide the whole line": "Скрывать строку целиком",
    "Comma (,)": "Запятая (,)",
    "Semicolon (;)": "Точка с запятой (;)",
    "Pipe (|)": "Вертикальная черта (|)",
    "Space character": "Пробел",
    "Custom…": "Другой…",
    "Delimiter character:": "Символ-разделитель:",
    "Side &panel": "&Боковая панель",
    "Show / hide the side panel": "Показать или скрыть боковую панель",

    # --- Строка состояния ---
    "RX %1   TX %2": "Принято %1   Отправлено %2",
    "Time since the interface was opened": "Сколько открыт интерфейс",

    # --- Настройки телеметрии ---
    "Telemetry delimiter": "Разделитель телеметрии",
    "Lines made only of numbers separated by this character can be hidden from the "
    "terminal — right-click the toolbar button for quick presets. Hidden lines still "
    "reach the chart, the search and the log.":
        "Строки из одних чисел через этот знак можно скрыть из терминала — быстрый выбор "
        "разделителя по правому клику на кнопке в панели. В график, поиск и журнал "
        "скрытые строки при этом попадают.",

    # --- Строка отправки ---
    "%1 of %2 — Tab for next": "%1 из %2 — Tab дальше",
    "%1 of %2": "%1 из %2",
    "Save as macro": "Сохранить как макрос",
    "Put into the send bar": "Положить в строку отправки",
    "Saved as a macro: %1": "Сохранено макросом: %1",
    "Macro: %1": "Макрос: %1",
    "Delete macros": "Удаление макросов",
    "Pick a preset or type the period in milliseconds":
        "Выберите готовый период или введите его в миллисекундах",
    "Find...": "Найти…",

    # --- Журналирование ---
    "Record": "Записывать",
    "Recording": "Идёт запись",
    "Recording unavailable": "Запись недоступна",
    "Everything": "Всё",
    "Messages only": "Только сообщения",
    "Telemetry only": "Только телеметрию",
    "Telemetry is a line made only of numbers separated by the delimiter set in Settings.":
        "Телеметрия — строка из одних чисел через разделитель, заданный в настройках.",
    "View in terminal": "Показать в терминале",
    "Reveal in Finder": "Показать в Finder",
    "Show in Explorer": "Показать в проводнике",
    "Open containing folder": "Открыть каталог",
    "Copy path": "Скопировать путь",
    "Delete log": "Удаление лога",
    "Delete %1 permanently?": "Удалить %1 безвозвратно?",
    "Cannot delete %1": "Не удалось удалить %1",
    "only %1 free": "свободно всего %1",
    "Files": "Файлы",
    "Contents": "Содержимое",
    "Leave empty to use the default location.": "Пусто — каталог по умолчанию.",
    "Placeholders: {interface}, {alias}, {date}, {time}.":
        "Подстановки: {interface}, {alias}, {date}, {time}.",
    "Colour codes make the file hard to read outside a terminal.":
        "Цветовые коды мешают читать файл вне терминала.",

    # --- Генератор ---
    "Sine": "Синус",
    "Square wave": "Меандр",
    "Triangle wave": "Треугольник",
    "Sawtooth wave": "Пила",
    "Wave period": "Период формы",
    "Amplitude": "Амплитуда",
    " packets": " посылок",
    "Period counted in packets, not milliseconds: the send interval is not kept exactly "
    "by the operating system, and a shape tied to the clock would drift.":
        "Период считается в посылках, а не в миллисекундах: интервал отправки система не "
        "выдерживает точно, и форма, привязанная к часам, поплыла бы.",

    # --- Плоттер ---
    "Plotter": "Плоттер",
    "Buffer": "Буфер",
    "Jump to the newest data": "Перейти к свежим данным",
    "Jump to the newest data and keep following it":
        "Перейти к свежим данным и следовать за ними",
    "Resume drawing; data kept coming while paused":
        "Продолжить отрисовку; данные копились и на паузе",
    "Freeze the picture, not the data": "Заморозить картинку, но не данные",
    "Field separator": "Разделитель полей",
    "Click to show or hide, double-click to change the colour":
        "Клик — показать или скрыть, двойной клик — сменить цвет",
    "Double-click to rename": "Двойной клик — переименовать",
    "Change colour…": "Сменить цвет…",
    "Rename…": "Переименовать…",
    "Set scale limits…": "Задать пределы шкалы…",
    "Change scale limits…": "Изменить пределы шкалы…",
    "Back to automatic scale": "Вернуть автомасштаб",
    "Clear this series only": "Очистить только этот ряд",
    "Scale limits": "Пределы шкалы",
    "Minimum for %1:": "Минимум для %1:",
    "Maximum for %1:": "Максимум для %1:",
    "Separator character:": "Символ-разделитель:",
    "What goes on the X axis": "Что отложено по оси X",
    "Open in a separate window": "Открыть отдельным окном",
    "Clear the collected data": "Очистить накопленное",
    "Copy the plot as an image. Right-click to save a file":
        "Скопировать график картинкой. Правый клик — сохранить в файл",
    "Save to file…": "Сохранить в файл…",
    "Save plot": "Сохранить график",
    "PNG image (*.png)": "Изображение PNG (*.png)",
    "CSV file (*.csv)": "Файл CSV (*.csv)",
    "Plot copied to the clipboard": "График скопирован в буфер обмена",
    "Nothing to export yet": "Экспортировать пока нечего",
    "Saved %1": "Сохранено: %1",
    "Cannot save %1": "Не удалось сохранить %1",
    "Follow new data": "Следовать за новыми данными",
    "Reset vertical zoom": "Сбросить масштаб по вертикали",
    "Whole buffer": "Весь буфер",
    "Last 1 s": "Последняя 1 с",
    "Last 10 s": "Последние 10 с",
    "Last 1 min": "Последняя 1 мин",
    "Last 10 min": "Последние 10 мин",
    " samples": " отсчётов",
    "How many samples to keep. What part of them is on screen is set by scrolling and "
    "zooming the plot itself.":
        "Сколько отсчётов хранить. Какая их часть на экране — решают прокрутка и масштаб "
        "самого графика.",
    "Spotty — plotter": "Spotty — плоттер",
    "Chart": "График",
    "CSV chart": "График CSV",
    "Spotty — chart": "Spotty — график",
    "Plots numeric lines from the output, such as \"12.5,3,-7\".":
        "Строит график по числовым строкам вывода — например, «12.5,3,-7».",
    "Waiting for numeric lines in the output": "Ждём числовые строки в выводе",
    "Series": "Ряд",
    "Series colour": "Цвет ряда",
    "Column %1": "Колонка %1",
    "Separator": "Разделитель",
    "Comma": "Запятая",
    "Semicolon": "Точка с запятой",
    "Tab": "Табуляция",
    "Window": "Окно",
    " points": " точек",
    "X axis": "Ось X",
    "Time": "Время",
    "Which column supplies X. Time is honest when the device does not send a coordinate "
    "of its own.":
        "Какая колонка даёт X. Время честнее, когда устройство не шлёт свою координату.",
    "Pause": "Пауза",
    "PAUSED": "ПАУЗА",
    "Freezes the picture, not the data: collecting continues.":
        "Замораживает картинку, но не сбор: накопление продолжается.",
    "Open in window": "Открыть в окне",
    "Last": "Последнее",
    "Min": "Мин",
    "Max": "Макс",
    "Avg": "Среднее",
    "Export CSV": "Выгрузить CSV",
    "Save PNG": "Сохранить PNG",
    "Clear": "Очистить",
    "Export chart data": "Выгрузка данных графика",
    "Save chart image": "Сохранение снимка графика",
    "CSV files (*.csv)": "Файлы CSV (*.csv)",
    "PNG images (*.png)": "Изображения PNG (*.png)",
    "There is nothing to export yet.": "Пока нечего выгружать.",
    "Could not write %1": "Не удалось записать %1",
    "Saved to %1": "Сохранено в %1",

    # --- Отправка файла ---
    "Send file": "Отправка файла",
    "Sends a file to the interface in chunks, optionally encoded as base64.":
        "Отправляет файл в интерфейс порциями, при желании перекодировав в base64.",
    "No file selected": "Файл не выбран",
    "Encoding": "Кодирование",
    "Raw bytes": "Как есть",
    "Base64, wrapped": "Base64 с переносами",
    "Hex, 16 bytes per line": "Hex, по 16 байт в строке",
    "Chunk": "Порция",
    "Terminator": "Завершитель",
    "Appended after the whole payload. Escapes: \\r \\n \\t":
        "Дописывается после всего содержимого. Escape-последовательности: \\r \\n \\t",
    "Cancel": "Отмена",
    "Cancelled after %1.": "Отменено после %1.",
    "Cannot open the file: %1": "Не удалось открыть файл: %1",
    "The file is empty.": "Файл пуст.",
    "Open the interface first.": "Сначала откройте интерфейс.",
    "Sending %1 (%2)": "Отправка %1 (%2)",
    "Sent %1.": "Отправлено %1.",
    "File sent.": "Файл отправлен.",
    "File transfer interrupted.": "Передача файла прервана.",
    "The interface closed after %1 — the file was sent only partially.":
        "Интерфейс закрылся после %1 — файл отправлен не целиком.",

    # --- Раздел «Plugins» в настройках ---
    "Plugins": "Плагины",
    "Plugin": "Плагин",
    "Details": "Подробности",
    "Panels and data": "Панели и обработка данных",
    "Rejected": "Отклонены",
    "Searched directories": "Просмотренные каталоги",
    "None — plugins are built into the executable.":
        "Нет — плагины вкомпилированы в исполняемый файл.",
    "Not a Spotty plugin of any known kind.": "Не плагин Spotty ни одного известного вида.",
    "Panel plugin reports an empty id.": "Панельный плагин сообщает пустой идентификатор.",
    "Another panel plugin already provides id \"%1\".":
        "Идентификатор «%1» уже занят другим панельным плагином.",
    "Panel id \"%1\" is already taken.": "Идентификатор панели «%1» уже занят.",
    "Plugin \"%1\" declares a panel with an empty id.":
        "Плагин «%1» объявил панель с пустым идентификатором.",
    "Built against panel API version %1, this build expects %2.":
        "Собран для версии панельного API %1, эта сборка ожидает %2.",

    # --- Прочее ---
    "Start / stop recording": "Начать или остановить запись",
}

#: Переводы форм множественного числа: source -> (одна, несколько, много).
#: Русский требует трёх форм, и Qt ожидает их именно в этом порядке.
PLURALS = {
    "%n s ago": ("%n секунду назад", "%n секунды назад", "%n секунд назад"),
    "%n min ago": ("%n минуту назад", "%n минуты назад", "%n минут назад"),
    "%n h ago": ("%n час назад", "%n часа назад", "%n часов назад"),
    "%n d ago": ("%n день назад", "%n дня назад", "%n дней назад"),
    "%n line(s)": ("%n строка", "%n строки", "%n строк"),
    "%n file(s)": ("%n файл", "%n файла", "%n файлов"),
    "%n series": ("%n ряд", "%n ряда", "%n рядов"),
    "%n point(s)": ("%n точка", "%n точки", "%n точек"),
    "Delete %n selected macro(s)?": ("Удалить выделенный макрос?",
                                     "Удалить %n выделенных макроса?",
                                     "Удалить %n выделенных макросов?"),
    "Export %n selected macro(s)": ("Выгрузить выделенный макрос",
                                    "Выгрузить %n выделенных макроса",
                                    "Выгрузить %n выделенных макросов"),
    "%n error(s)": ("%n ошибка", "%n ошибки", "%n ошибок"),
    "  ·  %n error(s)": ("  ·  %n ошибка", "  ·  %n ошибки", "  ·  %n ошибок"),
    "%n packet(s) sent": ("отправлена %n посылка", "отправлено %n посылки",
                          "отправлено %n посылок"),
    "%n macro(s) imported": ("импортирован %n макрос", "импортировано %n макроса",
                             "импортировано %n макросов"),
    "Delete %n selected log file(s) permanently?":
        ("Удалить выбранный файл журнала без возможности восстановления?",
         "Удалить %n выбранных файла журнала без возможности восстановления?",
         "Удалить %n выбранных файлов журнала без возможности восстановления?"),
    "Deleted %n log file(s).": ("Удалён %n файл журнала.",
                                 "Удалено %n файла журнала.",
                                 "Удалено %n файлов журнала."),
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
