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
    # Метка транспорта на кнопке скрытия: та же буква, что в колонке источника.
    "A": "A",
    "B": "B",
    "Hide output from interface %1. Its data is still parsed by panels, found by search "
    "and written to the log, and you can still send to it":
        "Скрыть вывод интерфейса %1. Его данные по-прежнему разбирают панели, находит "
        "поиск и пишет журнал, и отправлять в него можно как раньше",
    "Interface A": "Интерфейс A",
    "Interface B": "Интерфейс B",
    "Both": "Оба",
    "First available": "Первый доступный",
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
    "Where the repeated macro goes": "Куда уходит повторяемый макрос",

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
    "Add a highlight rule from the search pattern": "Добавить правило подсветки из образца поиска",
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
    "Where the stream goes": "Куда уходит поток",

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
    "The language, single-instance and enabled-plugin settings take effect after "
    "Spotty is restarted.":
        "Язык, режим единственного экземпляра и набор включённых плагинов вступят в силу "
        "после перезапуска Spotty.",
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

    # --- CLI поверх CAN (clican) ---
    "CLI over CAN (PCAN)": "CLI через CAN (PCAN)",
    "CAN bus": "Шина CAN",
    "Tunnel": "Туннель",
    "Bit rate": "Скорость",
    "%1 Mbit/s": "%1 Мбит/с",
    "%1 kbit/s": "%1 кбит/с",
    "Node": "Узел",
    "Boards answer the broadcast query while this window is open. A node that is silent "
    "right now can be entered by number.":
        "Пока это окно открыто, платы отвечают на широковещательный запрос. Молчащий "
        "сейчас узел можно указать номером.",
    "Keep-alive": "Удержание туннеля",
    "A board leaves tunnelling mode %1 ms after the last packet addressed to it and goes "
    "back to its own UART.":
        "Плата выходит из режима туннелирования через %1 мс после последнего пакета на "
        "её адрес и возвращается к своему UART.",
    "Response timeout": "Таймаут ответа",
    "Warn when the node stays silent for this long. The channel stays open - a rebooting "
    "board comes back on its own. Zero disables it.":
        "Предупредить, если узел молчит дольше этого времени. Канал при этом остаётся "
        "открытым — перезагружающаяся плата вернётся сама. Ноль отключает проверку.",
    "in use by another application": "занят другим приложением",
    "node %1, %2": "узел %1, %2",
    "no node, %1": "узел не выбран, %1",
    "Node %1": "Узел %1",
    "Node %1 - no answer": "Узел %1 — нет ответа",
    "Select a node between %1 and %2: the CAN tunnel addresses a specific board, not the "
    "bus as a whole.":
        "Выберите узел от %1 до %2: туннель CAN обращается к конкретной плате, а не к "
        "шине целиком.",
    "Could not reach node %1: %2": "Не удалось обратиться к узлу %1: %2",
    "Could not send to node %1: %2": "Не удалось отправить узлу %1: %2",
    "Node %1 has not answered for %2 ms": "Узел %1 не отвечает уже %2 мс",
    "Unsupported CAN bit rate: %1 bit/s": "Неподдерживаемая скорость CAN: %1 бит/с",
    "Could not open the CAN channel: %1": "Не удалось открыть канал CAN: %1",
    "The CAN channel is closed": "Канал CAN закрыт",
    "CAN bus error: %1": "Ошибка шины CAN: %1",

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

    # --- Генератор сигналов (signalgen) ---
    "Signal generator": "Генератор сигналов",
    "Virtual signal source": "Виртуальный источник сигналов",
    "Virtual math signal source": "Виртуальный источник математических сигналов",
    "Waveform": "Форма сигнала",
    "Cosine": "Косинус",
    "Square": "Меандр",
    "Triangle": "Треугольник",
    "Sawtooth": "Пила",
    "Noise": "Шум",
    "Chirp (rising frequency)": "Свип (растущая частота)",
    "Damped sine (retriggered)": "Затухающая синусоида (с перезапуском)",
    "Pulse train": "Импульсы",
    "Staircase": "Лестница",
    "All waveforms (6 columns)": "Все формы разом (6 колонок)",
    "Growing column count": "Растущее число колонок",
    "Period": "Период",
    "Amplitude": "Амплитуда",
    "Offset": "Смещение",
    "Sample interval": "Интервал отсчётов",
    "Output": "Вывод",
    "Prepend time column": "Первой колонкой — время",
    "Emit status text lines": "Слать текстовые строки состояния",
    "Added to every value; shifts the curve up or down.":
        "Прибавляется к каждому значению; поднимает или опускает кривую.",
    "How often a new line is emitted.": "Как часто выдаётся новая строка.",
    "Length of one cycle. Chirp and the retriggered sine use it as the starting period of "
    "a longer, repeating pattern.":
        "Длительность одного цикла. Свип и затухающая синусоида берут её как начальный "
        "период более длинного повторяющегося узора.",
    "Random jitter layered on top of the waveform, as a percentage of the amplitude.":
        "Случайный разброс поверх формы, в процентах от амплитуды.",
    "First column holds seconds elapsed since the channel opened - a steady reference axis, "
    "useful as a custom X axis in the chart panel.":
        "Первая колонка — секунды с открытия канала: ровная опорная ось, годится как своя "
        "ось X в плоттере.",
    "Occasionally sends a non-numeric line, like a device mixing log messages into "
    "telemetry - checks that the chart skips it instead of breaking.":
        "Изредка шлёт нечисловую строку, как устройство, мешающее сообщения с телеметрией, "
        "— проверяет, что плоттер её пропустит, а не сломается.",
    '"All waveforms" and "Growing column count" emit several columns at once - handy for '
    "testing the chart panel's series table, its colours, and how it reacts to a series "
    "appearing mid-stream.":
        "«Все формы разом» и «Растущее число колонок» выдают несколько колонок сразу — "
        "удобно проверять таблицу рядов плоттера, её цвета и то, как он встречает колонку, "
        "появившуюся посреди потока.",

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
    "What the plot shows": "Что показывает график",
    "Settings saved for a particular device":
        "Настройки, сохранённые под конкретное устройство",
    # Подсказка в пустом списке профилей: строчная буква отличает её от имени профиля.
    "unsaved profile": "профиль не сохранён",
    "Save the current settings as a profile": "Сохранить текущие настройки профилем",
    "Delete this profile": "Удалить этот профиль",
    "New profile": "Новый профиль",
    "Profile name:": "Имя профиля:",
    "That name cannot be used for a file": "Такое имя не годится для файла",
    "Could not save the profile": "Не удалось сохранить профиль",
    "Plotter profile: %1": "Профиль плоттера: %1",
    "Not enough data for a histogram": "Данных для гистограммы недостаточно",
    "No spectrum: %1": "Спектра нет: %1",
    "%1: μ %2, σ %3": "%1: μ %2, σ %3",
    "%1 Hz": "%1 Гц",
    "resampled to an even grid": "приведено к равномерной сетке",
    "Showing: %1": "Показано: %1",
    "Time series": "Развёртка по времени",
    "Multi-plot": "Мультиплот",
    "XY (phase)": "XY (фазовый)",
    "Cumulative": "Накопление",
    "Histogram": "Гистограмма",
    "Spectrum": "Спектр",
    "Values against time": "Значения от времени",
    "One small plot per series, sharing the time axis":
        "По мини-графику на ряд, ось времени общая",
    "One series against another; time is not used":
        "Один ряд против другого; время не участвует",
    "Running sum from the start of the buffer": "Бегущая сумма от начала буфера",
    "How often each value occurs": "Как часто встречается каждое значение",
    "Amplitude against frequency": "Амплитуда от частоты",
    "Pick a column for the X axis to see a phase plot":
        "Выберите колонку для оси X, чтобы увидеть фазовый портрет",
    "Click to show or hide, double-click to change the colour":
        "Клик — показать или скрыть, двойной клик — сменить цвет",
    "Hide every series at once; click again to bring back the previous selection":
        "Скрыть все ряды разом; повторный клик вернёт прежний выбор",
    "Double-click to rename": "Двойной клик — переименовать",
    "Change colour…": "Сменить цвет…",
    "Rename…": "Переименовать…",
    "Set scale limits…": "Задать пределы шкалы…",
    "Change scale limits…": "Изменить пределы шкалы…",
    "Back to automatic scale": "Вернуть автомасштаб",
    "Clear this series only": "Очистить только этот ряд",
    "Scale limits": "Пределы шкалы",
    "Scale limits — %1": "Пределы шкалы — %1",
    "Minimum": "Минимум",
    "Maximum": "Максимум",
    "Measured: %1 … %2": "Измерено: %1 … %2",
    "Use measured": "Взять измеренные",
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
    "Clearing a checkbox keeps the plugin out of the next start: a disabled interface "
    "plugin offers no devices, a disabled panel plugin builds no panels. Takes effect "
    "after restarting Spotty.":
        "Снятый флажок оставит плагин за бортом следующего запуска: выключенный плагин "
        "интерфейса не предложит ни одного устройства, выключенный плагин панели не "
        "построит ни одной панели. Вступает в силу после перезапуска Spotty.",
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

    # --- Разбор JSON (панель jsontree) ---
    "JSON": "JSON",
    "JSON tree": "Дерево JSON",
    "Field": "Поле",
    "Value": "Значение",
    "Rate": "Частота",
    "View": "Показ",
    "Framing": "Границы документов",
    "Pause parsing": "Приостановить разбор",
    "Resume parsing": "Продолжить разбор",
    "Clear the tree": "Очистить дерево",
    "Hide fields that stopped arriving": "Скрыть поля, которые перестали приходить",
    "Remove fields that stopped arriving": "Убрать поля, которые перестали приходить",
    "Expand everything": "Раскрыть всё",
    "Collapse everything": "Свернуть всё",
    "Expand everything below": "Раскрыть всё ниже",
    "Flash a field when its value changes\nRight-click to set how long it glows":
        "Подсвечивать поле при изменении значения\n"
        "Правая кнопка — насколько долго оно горит",
    "Flash duration": "Длительность вспышки",
    "Milliseconds:": "Миллисекунды:",
    "Other…": "Другое…",
    "The panel has a button for this too, and the same button sets the duration by "
    "right-click.":
        "В самой панели для этого есть кнопка, и она же задаёт длительность по правому "
        "щелчку.",
    "Copy value": "Копировать значение",
    "%1 fields · %2 documents": "полей: %1 · документов: %2",
    "Documents: %1\nText lines skipped: %2\nMalformed: %3\n"
    "Abandoned: %4\nPaths rejected by limits: %5":
        "Документов: %1\nПропущено текстовых строк: %2\nБитых: %3\n"
        "Брошено накоплений: %4\nОтвергнуто пределами: %5",
    "Node limit reached (%1). New fields are ignored. If the stream contains arrays, set "
    "the array identity field in settings — otherwise every element makes its own branch.":
        "Достигнут предел полей (%1). Новые поля не добавляются. Если в потоке есть "
        "массивы, задайте в настройках ключ идентификации — иначе каждый элемент заводит "
        "свою ветку.",
    "Array identity field": "Ключ идентификации массива",
    "Objects inside an array are told apart by this field, for example id. Leave it empty "
    "to collapse the array into a single branch showing the last element's values.":
        "По этому полю различаются объекты внутри массива — например, id. Пустое значение "
        "схлопывает массив в одну ветку со значениями последнего элемента.",
    "Field limit": "Предел числа полей",
    "Once reached, new fields are ignored and the ones already there keep updating. A "
    "stream that uses random ids will hit this quickly.":
        "По достижении новые поля не заводятся, а уже заведённые продолжают обновляться. "
        "Поток со случайными идентификаторами упирается в предел быстро.",
    "Maximum depth": "Наибольшая глубина",
    "Deeper subtrees are shown collapsed into a single value.":
        "Более глубокие поддеревья показываются свёрнутыми в одно значение.",
    "Children per branch": "Детей на ветку",
    "Multi-line JSON limit": "Предел многострочного JSON",
    "How many lines one pretty-printed document may span.":
        "Сколько строк может занимать один документ с отступами.",
    "Give up on unclosed JSON after": "Бросать незакрытый JSON через",
    "A lost closing brace would otherwise swallow everything that follows.":
        "Иначе потерянная закрывающая скобка проглотит весь последующий поток.",
    "Flash a field when its value changes": "Подсвечивать поле при изменении значения",

    # --- Виртуальный источник JSON (jsongen) ---
    "JSON source": "Источник JSON",
    "Virtual JSON source": "Виртуальный источник JSON",
    "Virtual JSON telemetry source": "Виртуальный источник телеметрии в JSON",
    "Shape": "Форма",
    "Document shape": "Форма документа",
    "Flat object": "Плоский объект",
    "Nested object": "Вложенный объект",
    "Array of objects": "Массив объектов",
    "Mixed (rotates through all three)": "Вперемешку (по кругу все три)",
    '"Mixed" alternates shapes from document to document - the case a parser is most '
    "likely to get wrong.":
        "«Вперемешку» меняет форму от документа к документу — случай, на котором разбор "
        "ошибается чаще всего.",
    "Fields per object": "Полей в объекте",
    "Nesting depth": "Глубина вложенности",
    "Only used by the nested and mixed shapes.":
        "Действует только для вложенной и смешанной формы.",
    "Objects per array": "Объектов в массиве",
    "Use random ids": "Случайные идентификаторы",
    "Every array element gets a brand new id, so the tree grows without bound - this is "
    "what a node limit is for.":
        "Каждый элемент массива получает новый идентификатор, и дерево растёт без предела "
        "— ровно то, ради чего предел и заведён.",
    "Document interval": "Период документов",
    "Different rates per field": "Разная частота у разных полей",
    "Some fields come with every document, some every 5th, some every 25th. A field that "
    "is not due is left out of the document entirely - without that there is nothing for "
    "a rate column to show.":
        "Одни поля приходят с каждым документом, другие с каждым пятым, третьи с каждым "
        "двадцать пятым. Поле, которому не подошёл черёд, из документа убирается целиком — "
        "иначе колонке частоты нечего было бы показывать.",
    "Corrupt documents": "Битые документы",
    "Truncated tails, missing braces, stray commas - a parser must skip these instead of "
    "breaking or littering its output.":
        "Обрубленные хвосты, потерянные скобки, лишние запятые — разбор обязан их "
        "пропускать, а не ломаться и не засорять вывод.",
    "Emit plain log lines": "Слать обычные текстовые строки",
    "Ordinary text mixed into the telemetry, the way real firmware does it.":
        "Обычный текст вперемешку с телеметрией — так ведёт себя настоящая прошивка.",
    "Layout": "Раскладка",
    "One document per line": "По документу на строку",
    "Pretty-printed over several lines": "С отступами на несколько строк",
    "Alternate between the two": "Чередовать оба",
    "Split documents into two packets": "Разбивать документ на два пакета",
    "Breaks the stream mid-line, the way a polled source does. A reader must wait for the "
    "line to finish instead of parsing half of it.":
        "Рвёт поток посреди строки, как это делает опрашиваемый источник. Читатель обязан "
        "дождаться конца строки, а не разбирать её половину.",
    "Drift": "Дрейф",
    "Structure drift": "Дрейф структуры",
    "Off": "Выключен",
    "Keep adding new keys": "Добавлять новые ключи",
    "Keys come and go": "Ключи появляются и исчезают",
    "Checks that a tree grows with the stream and shows what stopped coming.":
        "Проверяет, что дерево растёт вместе с потоком и показывает то, что перестало "
        "приходить.",
    "Drift every": "Дрейф каждые",
    "documents": "документов",
    "lines": "строк",

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
    "%n field(s) removed": ("убрано %n поле", "убрано %n поля", "убрано %n полей"),
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
    # &apos; сюда попадает от lupdate: апостроф в исходной строке он экранирует, а в
    # словаре ключи записаны обычным текстом, и без этой замены строка не находилась.
    return (text.replace("&quot;", '"').replace("&apos;", "'").replace("&amp;", "&")
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
