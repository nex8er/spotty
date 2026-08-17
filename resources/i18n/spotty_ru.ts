<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ru" sourcelanguage="en">
<context>
    <name>QObject</name>
    <message>
        <source>Unsupported CAN bit rate: %1 bit/s</source>
        <translation>Неподдерживаемая скорость CAN: %1 бит/с</translation>
    </message>
    <message>
        <source>Could not open the CAN channel: %1</source>
        <translation>Не удалось открыть канал CAN: %1</translation>
    </message>
    <message>
        <source>The CAN channel is closed</source>
        <translation>Канал CAN закрыт</translation>
    </message>
    <message>
        <source>CAN bus error: %1</source>
        <translation>Ошибка шины CAN: %1</translation>
    </message>
</context>
<context>
    <name>spotty::ChannelWorker</name>
    <message>
        <source>Could not open the interface.</source>
        <translation>Не удалось открыть интерфейс.</translation>
    </message>
    <message numerus="yes">
        <source>Could not send %n byte(s).</source>
        <translation>
            <numerusform>Не удалось отправить %n байт.</numerusform>
            <numerusform>Не удалось отправить %n байта.</numerusform>
            <numerusform>Не удалось отправить %n байт.</numerusform>
        </translation>
    </message>
    <message>
        <source>Could not reopen the interface.</source>
        <translation>Не удалось открыть интерфейс заново.</translation>
    </message>
</context>
<context>
    <name>spotty::CliCanChannel</name>
    <message>
        <source>Select a node between %1 and %2: the CAN tunnel addresses a specific board, not the bus as a whole.</source>
        <translation>Выберите узел от %1 до %2: туннель CAN обращается к конкретной плате, а не к шине целиком.</translation>
    </message>
    <message>
        <source>Could not reach node %1: %2</source>
        <translation>Не удалось обратиться к узлу %1: %2</translation>
    </message>
    <message>
        <source>Node %1</source>
        <translation>Узел %1</translation>
    </message>
    <message>
        <source>Could not send to node %1: %2</source>
        <translation>Не удалось отправить узлу %1: %2</translation>
    </message>
    <message>
        <source>Node %1 has not answered for %2 ms</source>
        <translation>Узел %1 не отвечает уже %2 мс</translation>
    </message>
    <message>
        <source>Node %1 - no answer</source>
        <translation>Узел %1 — нет ответа</translation>
    </message>
</context>
<context>
    <name>spotty::CliCanPlugin</name>
    <message>
        <source>%1 Mbit/s</source>
        <translation>%1 Мбит/с</translation>
    </message>
    <message>
        <source>%1 kbit/s</source>
        <translation>%1 кбит/с</translation>
    </message>
    <message>
        <source>in use by another application</source>
        <translation>занят другим приложением</translation>
    </message>
    <message>
        <source>CAN bus</source>
        <translation>Шина CAN</translation>
    </message>
    <message>
        <source>Tunnel</source>
        <translation>Туннель</translation>
    </message>
    <message>
        <source>Bit rate</source>
        <translation>Скорость</translation>
    </message>
    <message>
        <source>Node</source>
        <translation>Узел</translation>
    </message>
    <message>
        <source>Boards answer the broadcast query while this window is open. A node that is silent right now can be entered by number.</source>
        <translation>Пока это окно открыто, платы отвечают на широковещательный запрос. Молчащий сейчас узел можно указать номером.</translation>
    </message>
    <message>
        <source>Keep-alive</source>
        <translation>Удержание туннеля</translation>
    </message>
    <message>
        <source>ms</source>
        <translation>мс</translation>
    </message>
    <message>
        <source>A board leaves tunnelling mode %1 ms after the last packet addressed to it and goes back to its own UART.</source>
        <translation>Плата выходит из режима туннелирования через %1 мс после последнего пакета на её адрес и возвращается к своему UART.</translation>
    </message>
    <message>
        <source>Response timeout</source>
        <translation>Таймаут ответа</translation>
    </message>
    <message>
        <source>Warn when the node stays silent for this long. The channel stays open - a rebooting board comes back on its own. Zero disables it.</source>
        <translation>Предупредить, если узел молчит дольше этого времени. Канал при этом остаётся открытым — перезагружающаяся плата вернётся сама. Ноль отключает проверку.</translation>
    </message>
    <message>
        <source>no node, %1</source>
        <translation>узел не выбран, %1</translation>
    </message>
    <message>
        <source>node %1, %2</source>
        <translation>узел %1, %2</translation>
    </message>
    <message>
        <source>CLI over CAN (PCAN)</source>
        <translation>CLI через CAN (PCAN)</translation>
    </message>
</context>
<context>
    <name>spotty::DataCodec</name>
    <message>
        <source>Not valid Base64.</source>
        <translation>Некорректная запись Base64.</translation>
    </message>
    <message>
        <source>&quot;%1&quot; is not a hexadecimal digit.</source>
        <translation>«%1» не шестнадцатеричная цифра.</translation>
    </message>
    <message>
        <source>Odd number of hexadecimal digits - one byte is two digits.</source>
        <translation>Нечётное число шестнадцатеричных цифр — в байте их две.</translation>
    </message>
    <message>
        <source>Hex</source>
        <translation>Hex</translation>
    </message>
    <message>
        <source>Base64</source>
        <translation>Base64</translation>
    </message>
    <message>
        <source>Text</source>
        <translation>Текст</translation>
    </message>
    <message>
        <source>None</source>
        <translation>Нет</translation>
    </message>
</context>
<context>
    <name>spotty::DataGenerator</name>
    <message>
        <source>Random bytes</source>
        <translation>Случайные байты</translation>
    </message>
    <message>
        <source>Fixed byte</source>
        <translation>Постоянный байт</translation>
    </message>
    <message>
        <source>Ramp 00..FF</source>
        <translation>Пила 00..FF</translation>
    </message>
    <message>
        <source>ASCII text</source>
        <translation>Текст ASCII</translation>
    </message>
    <message>
        <source>Sine</source>
        <translation>Синус</translation>
    </message>
    <message>
        <source>Square wave</source>
        <translation>Меандр</translation>
    </message>
    <message>
        <source>Triangle wave</source>
        <translation>Треугольник</translation>
    </message>
    <message>
        <source>Sawtooth wave</source>
        <translation>Пила</translation>
    </message>
    <message>
        <source>Packet counter</source>
        <translation>Счётчик посылок</translation>
    </message>
</context>
<context>
    <name>spotty::FileSendPanel</name>
    <message>
        <source>Send file</source>
        <translation>Отправка файла</translation>
    </message>
    <message>
        <source>Sends a file to the interface in chunks, optionally encoded as base64.</source>
        <translation>Отправляет файл в интерфейс порциями, при желании перекодировав в base64.</translation>
    </message>
    <message>
        <source>No file selected</source>
        <translation>Файл не выбран</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>Обзор...</translation>
    </message>
    <message>
        <source>Raw bytes</source>
        <translation>Как есть</translation>
    </message>
    <message>
        <source>Base64</source>
        <translation>Base64</translation>
    </message>
    <message>
        <source>Base64, wrapped</source>
        <translation>Base64 с переносами</translation>
    </message>
    <message>
        <source>Hex</source>
        <translation>Hex</translation>
    </message>
    <message>
        <source>Hex, 16 bytes per line</source>
        <translation>Hex, по 16 байт в строке</translation>
    </message>
    <message>
        <source>Encoding</source>
        <translation>Кодирование</translation>
    </message>
    <message>
        <source> bytes</source>
        <translation> байт</translation>
    </message>
    <message>
        <source>Chunk</source>
        <translation>Порция</translation>
    </message>
    <message>
        <source>Appended after the whole payload. Escapes: \r \n \t</source>
        <translation>Дописывается после всего содержимого. Escape-последовательности: 
 
 	</translation>
    </message>
    <message>
        <source>Terminator</source>
        <translation>Завершитель</translation>
    </message>
    <message>
        <source>Cannot open the file: %1</source>
        <translation>Не удалось открыть файл: %1</translation>
    </message>
    <message>
        <source>The file is empty.</source>
        <translation>Файл пуст.</translation>
    </message>
    <message>
        <source>Cancelled after %1.</source>
        <translation>Отменено после %1.</translation>
    </message>
    <message>
        <source>Open the interface first.</source>
        <translation>Сначала откройте интерфейс.</translation>
    </message>
    <message>
        <source>Sending %1 (%2)</source>
        <translation>Отправка %1 (%2)</translation>
    </message>
    <message>
        <source>Sent %1.</source>
        <translation>Отправлено %1.</translation>
    </message>
    <message>
        <source>File sent.</source>
        <translation>Файл отправлен.</translation>
    </message>
    <message>
        <source>%1 of %2</source>
        <translation>%1 из %2</translation>
    </message>
    <message>
        <source>The interface closed after %1 — the file was sent only partially.</source>
        <translation>Интерфейс закрылся после %1 — файл отправлен не целиком.</translation>
    </message>
    <message>
        <source>File transfer interrupted.</source>
        <translation>Передача файла прервана.</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>Отмена</translation>
    </message>
    <message>
        <source>Send</source>
        <translation>Отправка</translation>
    </message>
    <message>
        <source>The interface is not open.</source>
        <translation>Интерфейс не открыт.</translation>
    </message>
</context>
<context>
    <name>spotty::FileSendPlugin</name>
    <message>
        <source>Send file</source>
        <translation>Отправка файла</translation>
    </message>
</context>
<context>
    <name>spotty::Formatting</name>
    <message>
        <source>just now</source>
        <translation>только что</translation>
    </message>
    <message numerus="yes">
        <source>%n s ago</source>
        <translation>
            <numerusform>%n секунду назад</numerusform>
            <numerusform>%n секунды назад</numerusform>
            <numerusform>%n секунд назад</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n min ago</source>
        <translation>
            <numerusform>%n минуту назад</numerusform>
            <numerusform>%n минуты назад</numerusform>
            <numerusform>%n минут назад</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n h ago</source>
        <translation>
            <numerusform>%n час назад</numerusform>
            <numerusform>%n часа назад</numerusform>
            <numerusform>%n часов назад</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n d ago</source>
        <translation>
            <numerusform>%n день назад</numerusform>
            <numerusform>%n дня назад</numerusform>
            <numerusform>%n дней назад</numerusform>
        </translation>
    </message>
    <message>
        <source>B</source>
        <translation>Б</translation>
    </message>
    <message>
        <source>KiB</source>
        <translation>КиБ</translation>
    </message>
    <message>
        <source>MiB</source>
        <translation>МиБ</translation>
    </message>
    <message>
        <source>GiB</source>
        <translation>ГиБ</translation>
    </message>
</context>
<context>
    <name>spotty::GeneratorPanel</name>
    <message>
        <source>Generator</source>
        <translation>Генератор</translation>
    </message>
    <message>
        <source>Pattern</source>
        <translation>Выражение</translation>
    </message>
    <message>
        <source> bytes</source>
        <translation> байт</translation>
    </message>
    <message>
        <source>Length</source>
        <translation>Длина</translation>
    </message>
    <message>
        <source>Byte value</source>
        <translation>Значение байта</translation>
    </message>
    <message>
        <source> packets</source>
        <translation> посылок</translation>
    </message>
    <message>
        <source>Period counted in packets, not milliseconds: the send interval is not kept exactly by the operating system, and a shape tied to the clock would drift.</source>
        <translation>Период считается в посылках, а не в миллисекундах: интервал отправки система не выдерживает точно, и форма, привязанная к часам, поплыла бы.</translation>
    </message>
    <message>
        <source>Wave period</source>
        <translation>Период формы</translation>
    </message>
    <message>
        <source>Amplitude</source>
        <translation>Амплитуда</translation>
    </message>
    <message>
        <source>%1 ms</source>
        <translation>%1 мс</translation>
    </message>
    <message>
        <source>Stream interval</source>
        <translation>Период потока</translation>
    </message>
    <message>
        <source>Preview</source>
        <translation>Предпросмотр</translation>
    </message>
    <message>
        <source>Send once</source>
        <translation>Отправить один раз</translation>
    </message>
    <message>
        <source>Stream</source>
        <translation>Поток</translation>
    </message>
    <message>
        <source>To send bar</source>
        <translation>В строку отправки</translation>
    </message>
    <message>
        <source>Put the generated data into the send bar without sending</source>
        <translation>Положить порождённые данные в строку отправки, не отправляя</translation>
    </message>
    <message numerus="yes">
        <source>%n packet(s) sent</source>
        <translation>
            <numerusform>отправлена %n посылка</numerusform>
            <numerusform>отправлено %n посылки</numerusform>
            <numerusform>отправлено %n посылок</numerusform>
        </translation>
    </message>
    <message>
        <source>Stop</source>
        <translation>Стоп</translation>
    </message>
</context>
<context>
    <name>spotty::GeneratorPlugin</name>
    <message>
        <source>Generator</source>
        <translation>Генератор</translation>
    </message>
</context>
<context>
    <name>spotty::InterfaceBar</name>
    <message>
        <source>Open</source>
        <translation>Открыт</translation>
    </message>
    <message>
        <source>Opening</source>
        <translation>Открывается</translation>
    </message>
    <message>
        <source>Unavailable</source>
        <translation>Недоступен</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Ошибка</translation>
    </message>
    <message>
        <source>Closed</source>
        <translation>Закрыт</translation>
    </message>
    <message>
        <source>Interface settings</source>
        <translation>Настройки интерфейса</translation>
    </message>
    <message>
        <source>Close the interface</source>
        <translation>Закрыть интерфейс</translation>
    </message>
    <message>
        <source>Open the interface</source>
        <translation>Открыть интерфейс</translation>
    </message>
    <message>
        <source>unavailable</source>
        <translation>недоступен</translation>
    </message>
    <message>
        <source>Not selected</source>
        <translation>Не выбрано</translation>
    </message>
</context>
<context>
    <name>spotty::InterfaceSettingsDialog</name>
    <message>
        <source>Interface settings</source>
        <translation>Настройки интерфейса</translation>
    </message>
</context>
<context>
    <name>spotty::InterfaceSettingsPanel</name>
    <message>
        <source>Device</source>
        <translation>Устройство</translation>
    </message>
    <message>
        <source>Interface</source>
        <translation>Интерфейс</translation>
    </message>
    <message>
        <source>Alias</source>
        <translation>Псевдоним</translation>
    </message>
    <message>
        <source>Hide from the interface list</source>
        <translation>Скрыть из списка интерфейсов</translation>
    </message>
    <message>
        <source>Address</source>
        <translation>Адрес</translation>
    </message>
    <message>
        <source>VID:PID</source>
        <translation>VID:PID</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Общие</translation>
    </message>
</context>
<context>
    <name>spotty::JlinkRttChannel</name>
    <message>
        <source>Could not open J-Link S/N %1: %2</source>
        <translation>Не удалось открыть J-Link S/N %1: %2</translation>
    </message>
    <message>
        <source>Could not connect to the target: %1</source>
        <translation>Не удалось подключиться к таргету: %1</translation>
    </message>
    <message>
        <source>Could not start RTT: %1</source>
        <translation>Не удалось запустить RTT: %1</translation>
    </message>
    <message>
        <source>RTT%1</source>
        <translation>RTT%1</translation>
    </message>
    <message>
        <source>RTT control block not found on the target yet — check that RTT is initialised in firmware and the target is running.</source>
        <translation>Управляющий блок RTT ещё не найден на таргете — проверьте, что RTT инициализирован в прошивке и таргет запущен.</translation>
    </message>
</context>
<context>
    <name>spotty::JlinkRttPlugin</name>
    <message>
        <source>J-Link</source>
        <translation>J-Link</translation>
    </message>
    <message>
        <source>Connection</source>
        <translation>Подключение</translation>
    </message>
    <message>
        <source>Target interface</source>
        <translation>Целевой интерфейс</translation>
    </message>
    <message>
        <source>SWD</source>
        <translation>SWD</translation>
    </message>
    <message>
        <source>JTAG</source>
        <translation>JTAG</translation>
    </message>
    <message>
        <source>Speed</source>
        <translation>Скорость</translation>
    </message>
    <message>
        <source>kHz</source>
        <translation>кГц</translation>
    </message>
    <message>
        <source>Target device</source>
        <translation>Целевое устройство</translation>
    </message>
    <message>
        <source>Exact SEGGER device name, e.g. &quot;NRF52832_XXAA&quot;. Required to connect. The J-Link device database was not found, so there are no suggestions — install SEGGER J-Link software for autocomplete.</source>
        <translation>Точное имя устройства SEGGER, например «NRF52832_XXAA». Обязательно для подключения. База устройств J-Link не найдена, подсказок не будет — установите SEGGER J-Link software для автодополнения.</translation>
    </message>
    <message>
        <source>Exact SEGGER device name, e.g. &quot;NRF52832_XXAA&quot; — start typing to search. Required to connect.</source>
        <translation>Точное имя устройства SEGGER, например «NRF52832_XXAA» — начните вводить для поиска. Обязательно для подключения.</translation>
    </message>
    <message>
        <source>%1 %2 kHz</source>
        <translation>%1 %2 кГц</translation>
    </message>
    <message>
        <source>J-Link RTT</source>
        <translation>J-Link RTT</translation>
    </message>
</context>
<context>
    <name>spotty::LogWriter</name>
    <message>
        <source>Cannot create the log directory: %1</source>
        <translation>Не удалось создать каталог логов: %1</translation>
    </message>
    <message>
        <source>Cannot open the log file: %1</source>
        <translation>Не удалось открыть файл лога: %1</translation>
    </message>
    <message>
        <source>Log write failed: %1</source>
        <translation>Ошибка записи лога: %1</translation>
    </message>
</context>
<context>
    <name>spotty::LoggingPanel</name>
    <message>
        <source>Logging</source>
        <translation>Логирование</translation>
    </message>
    <message>
        <source>Strip ANSI escape sequences</source>
        <translation>Убирать коды ANSI</translation>
    </message>
    <message>
        <source>Colour codes make the file hard to read outside a terminal and break searching through it.</source>
        <translation>Цветовые коды мешают читать файл вне терминала и ломают поиск по нему.</translation>
    </message>
    <message>
        <source>Include sent data</source>
        <translation>Записывать отправленное</translation>
    </message>
    <message>
        <source>Everything</source>
        <translation>Всё</translation>
    </message>
    <message>
        <source>Messages only</source>
        <translation>Только сообщения</translation>
    </message>
    <message>
        <source>Telemetry only</source>
        <translation>Только телеметрию</translation>
    </message>
    <message>
        <source>Telemetry is a line made only of numbers separated by the delimiter set in Settings.</source>
        <translation>Телеметрия — строка из одних чисел через разделитель, заданный в настройках.</translation>
    </message>
    <message>
        <source>Record</source>
        <translation>Записывать</translation>
    </message>
    <message>
        <source>Recent logs</source>
        <translation>Последние логи</translation>
    </message>
    <message>
        <source>Recording to %1</source>
        <translation>Запись в %1</translation>
    </message>
    <message>
        <source>Start / stop recording</source>
        <translation>Начать или остановить запись</translation>
    </message>
    <message>
        <source>Select an interface before recording.</source>
        <translation>Выберите интерфейс перед началом записи.</translation>
    </message>
    <message>
        <source>Recording unavailable</source>
        <translation>Запись недоступна</translation>
    </message>
    <message>
        <source>Recording</source>
        <translation>Идёт запись</translation>
    </message>
    <message>
        <source>View in terminal</source>
        <translation>Показать в терминале</translation>
    </message>
    <message>
        <source>Show in Explorer</source>
        <translation>Показать в проводнике</translation>
    </message>
    <message>
        <source>Copy path</source>
        <translation>Скопировать путь</translation>
    </message>
    <message numerus="yes">
        <source>%n file(s)</source>
        <translation>
            <numerusform>%n файл</numerusform>
            <numerusform>%n файла</numerusform>
            <numerusform>%n файлов</numerusform>
        </translation>
    </message>
    <message>
        <source>only %1 free</source>
        <translation>свободно всего %1</translation>
    </message>
    <message>
        <source>Stop recording</source>
        <translation>Остановить запись</translation>
    </message>
    <message>
        <source>Start recording</source>
        <translation>Начать запись</translation>
    </message>
    <message>
        <source>Use the context menu to view in the terminal. Drag out or press Ctrl+C to copy the file itself.</source>
        <translation>Открывайте файл в терминале через контекстное меню. Перетаскивание или Ctrl+C копируют сам файл.</translation>
    </message>
    <message>
        <source>Copy file</source>
        <translation>Скопировать файл</translation>
    </message>
    <message>
        <source>Delete selected</source>
        <translation>Удалить выбранные</translation>
    </message>
    <message>
        <source>Delete logs</source>
        <translation>Удаление логов</translation>
    </message>
    <message numerus="yes">
        <source>Delete %n selected log file(s) permanently?</source>
        <translation>
            <numerusform>Удалить выбранный файл журнала без возможности восстановления?</numerusform>
            <numerusform>Удалить %n выбранных файла журнала без возможности восстановления?</numerusform>
            <numerusform>Удалить %n выбранных файлов журнала без возможности восстановления?</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Deleted %n log file(s).</source>
        <translation>
            <numerusform>Удалён %n файл журнала.</numerusform>
            <numerusform>Удалено %n файла журнала.</numerusform>
            <numerusform>Удалено %n файлов журнала.</numerusform>
        </translation>
    </message>
    <message>
        <source>Could not delete: %1</source>
        <translation>Не удалось удалить: %1</translation>
    </message>
    <message>
        <source>%1
%2, %3</source>
        <translation>%1
%2, %3</translation>
    </message>
</context>
<context>
    <name>spotty::LoggingPlugin</name>
    <message>
        <source>Logging</source>
        <translation>Логирование</translation>
    </message>
    <message>
        <source>Log directory</source>
        <translation>Каталог логов</translation>
    </message>
    <message>
        <source>Files</source>
        <translation>Файлы</translation>
    </message>
    <message>
        <source>Leave empty to use the default location.</source>
        <translation>Пусто — каталог по умолчанию.</translation>
    </message>
    <message>
        <source>File name</source>
        <translation>Имя файла</translation>
    </message>
    <message>
        <source>Placeholders: {interface}, {alias}, {date}, {time}.</source>
        <translation>Подстановки: {interface}, {alias}, {date}, {time}.</translation>
    </message>
    <message>
        <source>Strip ANSI escape sequences</source>
        <translation>Убирать коды ANSI</translation>
    </message>
    <message>
        <source>Contents</source>
        <translation>Содержимое</translation>
    </message>
    <message>
        <source>Colour codes make the file hard to read outside a terminal.</source>
        <translation>Цветовые коды мешают читать файл вне терминала.</translation>
    </message>
    <message>
        <source>Include sent data</source>
        <translation>Записывать отправленное</translation>
    </message>
    <message>
        <source>Start recording when the interface opens</source>
        <translation>Начинать запись при открытии интерфейса</translation>
    </message>
</context>
<context>
    <name>spotty::LoopbackChannel</name>
    <message>
        <source>Virtual channel</source>
        <translation>Виртуальный канал</translation>
    </message>
</context>
<context>
    <name>spotty::LoopbackPlugin</name>
    <message>
        <source>Virtual echo channel</source>
        <translation>Виртуальный канал-эхо</translation>
    </message>
    <message>
        <source>Virtual data source</source>
        <translation>Виртуальный источник данных</translation>
    </message>
    <message>
        <source>Mode</source>
        <translation>Режим</translation>
    </message>
    <message>
        <source>Behaviour</source>
        <translation>Поведение</translation>
    </message>
    <message>
        <source>As the device implies</source>
        <translation>По устройству</translation>
    </message>
    <message>
        <source>Echo what is sent</source>
        <translation>Возвращать отправленное</translation>
    </message>
    <message>
        <source>Emit generated data</source>
        <translation>Выдавать порождённые данные</translation>
    </message>
    <message>
        <source>Silent</source>
        <translation>Молчать</translation>
    </message>
    <message>
        <source>loopback0 echoes what is sent, loopback1 emits generated lines.</source>
        <translation>loopback0 возвращает отправленное, loopback1 выдаёт сгенерированные строки.</translation>
    </message>
    <message>
        <source>Echo delay</source>
        <translation>Задержка эха</translation>
    </message>
    <message>
        <source>ms</source>
        <translation>мс</translation>
    </message>
    <message>
        <source>Simulates a device that takes time to answer.</source>
        <translation>Изображает устройство, которому нужно время на ответ.</translation>
    </message>
    <message>
        <source>Emit interval</source>
        <translation>Период выдачи</translation>
    </message>
    <message>
        <source>Include ANSI colour codes</source>
        <translation>Добавлять цветовые коды ANSI</translation>
    </message>
    <message>
        <source>Loopback</source>
        <translation>Петля</translation>
    </message>
</context>
<context>
    <name>spotty::MacrosPanel</name>
    <message>
        <source>%1 ms</source>
        <translation>%1 мс</translation>
    </message>
    <message>
        <source>%1 s</source>
        <translation>%1 с</translation>
    </message>
    <message>
        <source>Macros</source>
        <translation>Макросы</translation>
    </message>
    <message>
        <source>Macro preset; each preset is a separate file</source>
        <translation>Набор макросов; каждый набор — отдельный файл</translation>
    </message>
    <message>
        <source>New preset</source>
        <translation>Новый набор</translation>
    </message>
    <message>
        <source>Delete preset</source>
        <translation>Удалить набор</translation>
    </message>
    <message>
        <source>Add macro</source>
        <translation>Добавить макрос</translation>
    </message>
    <message>
        <source>Pick a preset or type the period in milliseconds</source>
        <translation>Выберите готовый период или введите его в миллисекундах</translation>
    </message>
    <message>
        <source>Saved as a macro: %1</source>
        <translation>Сохранено макросом: %1</translation>
    </message>
    <message>
        <source>Macro: %1</source>
        <translation>Макрос: %1</translation>
    </message>
    <message>
        <source>Delete macros</source>
        <translation>Удаление макросов</translation>
    </message>
    <message numerus="yes">
        <source>Delete %n selected macro(s)?</source>
        <translation>
            <numerusform>Удалить выделенный макрос?</numerusform>
            <numerusform>Удалить %n выделенных макроса?</numerusform>
            <numerusform>Удалить %n выделенных макросов?</numerusform>
        </translation>
    </message>
    <message>
        <source>Macro files (*.json)</source>
        <translation>Файлы макросов (*.json)</translation>
    </message>
    <message>
        <source>Could not read %1.</source>
        <translation>Не удалось прочитать %1.</translation>
    </message>
    <message numerus="yes">
        <source>%n macro(s) imported</source>
        <translation>
            <numerusform>импортирован %n макрос</numerusform>
            <numerusform>импортировано %n макроса</numerusform>
            <numerusform>импортировано %n макросов</numerusform>
        </translation>
    </message>
    <message>
        <source>There is nothing to export.</source>
        <translation>Экспортировать нечего.</translation>
    </message>
    <message numerus="yes">
        <source>Export %n selected macro(s)</source>
        <translation>
            <numerusform>Выгрузить выделенный макрос</numerusform>
            <numerusform>Выгрузить %n выделенных макроса</numerusform>
            <numerusform>Выгрузить %n выделенных макросов</numerusform>
        </translation>
    </message>
    <message>
        <source>Could not write %1.</source>
        <translation>Не удалось записать %1.</translation>
    </message>
    <message>
        <source>Exported to %1</source>
        <translation>Экспортировано в %1</translation>
    </message>
    <message>
        <source>New macro</source>
        <translation>Новый макрос</translation>
    </message>
    <message>
        <source>Import macros...</source>
        <translation>Импортировать...</translation>
    </message>
    <message>
        <source>Export macros...</source>
        <translation>Экспортировать...</translation>
    </message>
    <message>
        <source>Put into the send bar</source>
        <translation>Положить в строку отправки</translation>
    </message>
    <message>
        <source>Duplicate</source>
        <translation>Дублировать</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>Удалить</translation>
    </message>
    <message>
        <source>Format</source>
        <translation>Формат</translation>
    </message>
    <message>
        <source>Termination</source>
        <translation>Терминация</translation>
    </message>
    <message>
        <source>Delete macro</source>
        <translation>Удалить макрос</translation>
    </message>
    <message>
        <source>Export macros</source>
        <translation>Экспорт макросов</translation>
    </message>
    <message>
        <source>Import macros</source>
        <translation>Импорт макросов</translation>
    </message>
    <message>
        <source>Repeat</source>
        <translation>Повтор</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>Имя</translation>
    </message>
    <message>
        <source>Could not create a preset with that name.</source>
        <translation>Не удалось создать набор с таким именем.</translation>
    </message>
    <message>
        <source>Delete preset &quot;%1&quot; and its file?</source>
        <translation>Удалить набор «%1» вместе с файлом?</translation>
    </message>
    <message>
        <source>Stop repeating</source>
        <translation>Остановить повтор</translation>
    </message>
    <message>
        <source>Start repeating</source>
        <translation>Начать повтор</translation>
    </message>
    <message>
        <source>%1 · %2
Double-click to edit, right-click for options</source>
        <translation>%1 · %2
Двойной щелчок — правка, правая кнопка — действия</translation>
    </message>
    <message>
        <source>Send now</source>
        <translation>Отправить</translation>
    </message>
    <message>
        <source>The interface is not open.</source>
        <translation>Интерфейс не открыт.</translation>
    </message>
    <message>
        <source>Macro &quot;%1&quot;: %2</source>
        <translation>Макрос «%1»: %2</translation>
    </message>
    <message>
        <source>actual: %1 ms</source>
        <translation>фактически: %1 мс</translation>
    </message>
</context>
<context>
    <name>spotty::MacrosPlugin</name>
    <message>
        <source>Macros</source>
        <translation>Макросы</translation>
    </message>
</context>
<context>
    <name>spotty::MainWindow</name>
    <message>
        <source>Back to live output</source>
        <translation>К живому выводу</translation>
    </message>
    <message>
        <source>Fill in the required settings to open this interface.</source>
        <translation>Заполните обязательные настройки, чтобы открыть этот интерфейс.</translation>
    </message>
    <message>
        <source>Show data as a hexadecimal dump</source>
        <translation>Показать данные шестнадцатеричным дампом</translation>
    </message>
    <message>
        <source>Show timestamps</source>
        <translation>Показывать метки времени</translation>
    </message>
    <message>
        <source>Show transmit and receive marks</source>
        <translation>Показывать метки приёма и передачи</translation>
    </message>
    <message>
        <source>Echo sent data into the terminal</source>
        <translation>Отражать отправленное в терминале</translation>
    </message>
    <message>
        <source>Show line numbers</source>
        <translation>Показывать номера строк</translation>
    </message>
    <message>
        <source>Clear the terminal</source>
        <translation>Очистить терминал</translation>
    </message>
    <message>
        <source>Follow output</source>
        <translation>Следовать за выводом</translation>
    </message>
    <message>
        <source>One interface</source>
        <translation>Один интерфейс</translation>
    </message>
    <message>
        <source>Two interfaces</source>
        <translation>Два интерфейса</translation>
    </message>
    <message>
        <source>What the output area shows</source>
        <translation>Что показано в области вывода</translation>
    </message>
    <message>
        <source>&amp;File</source>
        <translation>&amp;Файл</translation>
    </message>
    <message>
        <source>&amp;Settings...</source>
        <translation>&amp;Настройки...</translation>
    </message>
    <message>
        <source>&amp;Quit</source>
        <translation>В&amp;ыход</translation>
    </message>
    <message>
        <source>&amp;Interface</source>
        <translation>&amp;Интерфейс</translation>
    </message>
    <message>
        <source>&amp;Open / Close</source>
        <translation>&amp;Открыть / закрыть</translation>
    </message>
    <message>
        <source>Toggle &amp;DTR</source>
        <translation>Переключить &amp;DTR</translation>
    </message>
    <message>
        <source>Toggle &amp;RTS</source>
        <translation>Переключить &amp;RTS</translation>
    </message>
    <message>
        <source>Send &amp;Break</source>
        <translation>Послать &amp;BREAK</translation>
    </message>
    <message>
        <source>&amp;View</source>
        <translation>&amp;Вид</translation>
    </message>
    <message>
        <source>&amp;Hexadecimal dump</source>
        <translation>&amp;Шестнадцатеричный дамп</translation>
    </message>
    <message>
        <source>&amp;Timestamps</source>
        <translation>&amp;Метки времени</translation>
    </message>
    <message>
        <source>C&amp;lear terminal</source>
        <translation>О&amp;чистить терминал</translation>
    </message>
    <message>
        <source>Time since the interface was opened</source>
        <translation>Сколько открыт интерфейс</translation>
    </message>
    <message>
        <source>Focus &amp;send bar</source>
        <translation>Фокус в &amp;строку отправки</translation>
    </message>
    <message>
        <source>Hide telemetry lines: values separated by the delimiter set in Settings. Right-click to change the delimiter</source>
        <translation>Скрывать телеметрию: числа через разделитель, заданный в настройках. Разделитель можно сменить по правому клику</translation>
    </message>
    <message>
        <source>Hide unreadable characters: control codes and invalid encoding. Right-click to choose how</source>
        <translation>Скрывать нечитаемые символы: управляющие коды и битую кодировку. Способ показа — по правому клику</translation>
    </message>
    <message>
        <source>Comma (,)</source>
        <translation>Запятая (,)</translation>
    </message>
    <message>
        <source>Semicolon (;)</source>
        <translation>Точка с запятой (;)</translation>
    </message>
    <message>
        <source>Tab</source>
        <translation>Табуляция</translation>
    </message>
    <message>
        <source>Space character</source>
        <translation>Пробел</translation>
    </message>
    <message>
        <source>Pipe (|)</source>
        <translation>Вертикальная черта (|)</translation>
    </message>
    <message>
        <source>Custom…</source>
        <translation>Другой…</translation>
    </message>
    <message>
        <source>Telemetry delimiter</source>
        <translation>Разделитель телеметрии</translation>
    </message>
    <message>
        <source>Delimiter character:</source>
        <translation>Символ-разделитель:</translation>
    </message>
    <message>
        <source>Show as dots</source>
        <translation>Точками</translation>
    </message>
    <message>
        <source>Hide</source>
        <translation>Скрывать</translation>
    </message>
    <message>
        <source>Hide the whole line</source>
        <translation>Скрывать строку целиком</translation>
    </message>
    <message>
        <source>&amp;Theme</source>
        <translation>&amp;Тема</translation>
    </message>
    <message>
        <source>&amp;Dark</source>
        <translation>&amp;Тёмная</translation>
    </message>
    <message>
        <source>&amp;Light</source>
        <translation>&amp;Светлая</translation>
    </message>
    <message>
        <source>&amp;Help</source>
        <translation>&amp;Справка</translation>
    </message>
    <message>
        <source>&amp;About Spotty</source>
        <translation>&amp;О программе</translation>
    </message>
    <message>
        <source>About Spotty</source>
        <translation>О программе</translation>
    </message>
    <message>
        <source>&lt;b&gt;Spotty %1&lt;/b&gt;&lt;br&gt;Modular terminal port monitor.&lt;br&gt;&lt;br&gt;Configuration: %2</source>
        <translation>&lt;b&gt;Spotty %1&lt;/b&gt;&lt;br&gt;Модульный терминал-монитор портов.&lt;br&gt;&lt;br&gt;Настройки: %2</translation>
    </message>
    <message>
        <source>Open</source>
        <translation>Открыт</translation>
    </message>
    <message>
        <source>Opening</source>
        <translation>Открывается</translation>
    </message>
    <message>
        <source>Closed</source>
        <translation>Закрыт</translation>
    </message>
    <message>
        <source>Unavailable</source>
        <translation>Недоступен</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Ошибка</translation>
    </message>
    <message>
        <source>This log file is empty</source>
        <translation>Этот файл лога пуст</translation>
    </message>
    <message>
        <source>Interface is open — waiting for data</source>
        <translation>Интерфейс открыт — ожидание данных</translation>
    </message>
    <message>
        <source>Opening the interface…</source>
        <translation>Открытие интерфейса…</translation>
    </message>
    <message>
        <source>The interface could not be opened</source>
        <translation>Интерфейс открыть не удалось</translation>
    </message>
    <message>
        <source>Choose an interface above to see its output here</source>
        <translation>Выберите интерфейс наверху, чтобы увидеть здесь его вывод</translation>
    </message>
    <message>
        <source>RX %1   TX %2</source>
        <translation>Принято %1   Отправлено %2</translation>
    </message>
    <message>
        <source>%1/s</source>
        <translation>%1/с</translation>
    </message>
    <message numerus="yes">
        <source>%n error(s)</source>
        <translation>
            <numerusform>%n ошибка</numerusform>
            <numerusform>%n ошибки</numerusform>
            <numerusform>%n ошибок</numerusform>
        </translation>
    </message>
    <message>
        <source>Settings</source>
        <translation>Настройки</translation>
    </message>
    <message>
        <source>Side &amp;panel</source>
        <translation>&amp;Боковая панель</translation>
    </message>
    <message>
        <source>The language and single-instance settings take effect after Spotty is restarted.</source>
        <translation>Язык и режим единственного экземпляра вступят в силу после перезапуска Spotty.</translation>
    </message>
    <message>
        <source>Settings, interfaces and history have been reset to defaults.</source>
        <translation>Настройки, интерфейсы и история сброшены к умолчаниям.</translation>
    </message>
    <message>
        <source>Control lines: uppercase means asserted</source>
        <translation>Линии управления: заглавные — линия поднята</translation>
    </message>
    <message>
        <source>Cannot open %1: %2</source>
        <translation>Не удалось открыть %1: %2</translation>
    </message>
    <message>
        <source>Viewing log: %1</source>
        <translation>Просмотр лога: %1</translation>
    </message>
</context>
<context>
    <name>spotty::PanelPluginRegistry</name>
    <message>
        <source>Built against panel API version %1, this build expects %2.</source>
        <translation>Собран для версии панельного API %1, эта сборка ожидает %2.</translation>
    </message>
    <message>
        <source>Panel plugin reports an empty id.</source>
        <translation>Панельный плагин сообщает пустой идентификатор.</translation>
    </message>
    <message>
        <source>Another panel plugin already provides id &quot;%1&quot;.</source>
        <translation>Идентификатор «%1» уже занят другим панельным плагином.</translation>
    </message>
    <message>
        <source>Plugin &quot;%1&quot; declares a panel with an empty id.</source>
        <translation>Плагин «%1» объявил панель с пустым идентификатором.</translation>
    </message>
    <message>
        <source>Panel id &quot;%1&quot; is already taken.</source>
        <translation>Идентификатор панели «%1» уже занят.</translation>
    </message>
</context>
<context>
    <name>spotty::PlotCanvas</name>
    <message>
        <source>Pause</source>
        <translation>Пауза</translation>
    </message>
    <message>
        <source>Freezes the picture, not the data: collecting continues.</source>
        <translation>Замораживает картинку, но не сбор: накопление продолжается.</translation>
    </message>
    <message>
        <source>Jump to the newest data</source>
        <translation>Перейти к свежим данным</translation>
    </message>
    <message>
        <source>Clear</source>
        <translation>Очистить</translation>
    </message>
    <message>
        <source>Last 1 s</source>
        <translation>Последняя 1 с</translation>
    </message>
    <message>
        <source>Last 10 s</source>
        <translation>Последние 10 с</translation>
    </message>
    <message>
        <source>Last 1 min</source>
        <translation>Последняя 1 мин</translation>
    </message>
    <message>
        <source>Last 10 min</source>
        <translation>Последние 10 мин</translation>
    </message>
    <message>
        <source>Whole buffer</source>
        <translation>Весь буфер</translation>
    </message>
    <message>
        <source>Follow new data</source>
        <translation>Следовать за новыми данными</translation>
    </message>
    <message>
        <source>Reset vertical zoom</source>
        <translation>Сбросить масштаб по вертикали</translation>
    </message>
    <message>
        <source>Pick a column for the X axis to see a phase plot</source>
        <translation>Выберите колонку для оси X, чтобы увидеть фазовый портрет</translation>
    </message>
    <message>
        <source>Not enough data for a histogram</source>
        <translation>Данных для гистограммы недостаточно</translation>
    </message>
    <message>
        <source>%1: μ %2, σ %3</source>
        <translation>%1: μ %2, σ %3</translation>
    </message>
    <message>
        <source>No spectrum: %1</source>
        <translation>Спектра нет: %1</translation>
    </message>
    <message>
        <source>%1 Hz</source>
        <translation>%1 Гц</translation>
    </message>
    <message>
        <source>resampled to an even grid</source>
        <translation>приведено к равномерной сетке</translation>
    </message>
    <message>
        <source>Waiting for numeric lines in the output</source>
        <translation>Ждём числовые строки в выводе</translation>
    </message>
    <message>
        <source>PAUSED</source>
        <translation>ПАУЗА</translation>
    </message>
</context>
<context>
    <name>spotty::PlotWidget</name>
    <message>
        <source>Jump to the newest data and keep following it</source>
        <translation>Перейти к свежим данным и следовать за ними</translation>
    </message>
    <message>
        <source>What the plot shows</source>
        <translation>Что показывает график</translation>
    </message>
    <message>
        <source>Field separator</source>
        <translation>Разделитель полей</translation>
    </message>
    <message>
        <source>What goes on the X axis</source>
        <translation>Что отложено по оси X</translation>
    </message>
    <message>
        <source>Export CSV</source>
        <translation>Выгрузить CSV</translation>
    </message>
    <message>
        <source>Copy the plot as an image. Right-click to save a file</source>
        <translation>Скопировать график картинкой. Правый клик — сохранить в файл</translation>
    </message>
    <message>
        <source>Save to file…</source>
        <translation>Сохранить в файл…</translation>
    </message>
    <message>
        <source>Open in a separate window</source>
        <translation>Открыть отдельным окном</translation>
    </message>
    <message>
        <source>Clear the collected data</source>
        <translation>Очистить накопленное</translation>
    </message>
    <message>
        <source>Resume drawing; data kept coming while paused</source>
        <translation>Продолжить отрисовку; данные копились и на паузе</translation>
    </message>
    <message>
        <source>Freeze the picture, not the data</source>
        <translation>Заморозить картинку, но не данные</translation>
    </message>
    <message>
        <source>Time series</source>
        <translation>Развёртка по времени</translation>
    </message>
    <message>
        <source>Multi-plot</source>
        <translation>Мультиплот</translation>
    </message>
    <message>
        <source>XY (phase)</source>
        <translation>XY (фазовый)</translation>
    </message>
    <message>
        <source>Cumulative</source>
        <translation>Накопление</translation>
    </message>
    <message>
        <source>Histogram</source>
        <translation>Гистограмма</translation>
    </message>
    <message>
        <source>Spectrum</source>
        <translation>Спектр</translation>
    </message>
    <message>
        <source>Values against time</source>
        <translation>Значения от времени</translation>
    </message>
    <message>
        <source>One small plot per series, sharing the time axis</source>
        <translation>По мини-графику на ряд, ось времени общая</translation>
    </message>
    <message>
        <source>One series against another; time is not used</source>
        <translation>Один ряд против другого; время не участвует</translation>
    </message>
    <message>
        <source>Running sum from the start of the buffer</source>
        <translation>Бегущая сумма от начала буфера</translation>
    </message>
    <message>
        <source>How often each value occurs</source>
        <translation>Как часто встречается каждое значение</translation>
    </message>
    <message>
        <source>Amplitude against frequency</source>
        <translation>Амплитуда от частоты</translation>
    </message>
    <message>
        <source>Showing: %1</source>
        <translation>Показано: %1</translation>
    </message>
    <message>
        <source>Comma (,)</source>
        <translation>Запятая (,)</translation>
    </message>
    <message>
        <source>Semicolon (;)</source>
        <translation>Точка с запятой (;)</translation>
    </message>
    <message>
        <source>Tab</source>
        <translation>Табуляция</translation>
    </message>
    <message>
        <source>Space character</source>
        <translation>Пробел</translation>
    </message>
    <message>
        <source>Pipe (|)</source>
        <translation>Вертикальная черта (|)</translation>
    </message>
    <message>
        <source>Custom…</source>
        <translation>Другой…</translation>
    </message>
    <message>
        <source>Separator character:</source>
        <translation>Символ-разделитель:</translation>
    </message>
    <message>
        <source>Time</source>
        <translation>Время</translation>
    </message>
    <message>
        <source>Plot copied to the clipboard</source>
        <translation>График скопирован в буфер обмена</translation>
    </message>
    <message>
        <source>Save plot</source>
        <translation>Сохранить график</translation>
    </message>
    <message>
        <source>PNG image (*.png)</source>
        <translation>Изображение PNG (*.png)</translation>
    </message>
    <message>
        <source>Saved %1</source>
        <translation>Сохранено: %1</translation>
    </message>
    <message>
        <source>Cannot save %1</source>
        <translation>Не удалось сохранить %1</translation>
    </message>
    <message>
        <source>Nothing to export yet</source>
        <translation>Экспортировать пока нечего</translation>
    </message>
    <message>
        <source>CSV file (*.csv)</source>
        <translation>Файл CSV (*.csv)</translation>
    </message>
</context>
<context>
    <name>spotty::PlotterPanel</name>
    <message>
        <source>Plotter</source>
        <translation>Плоттер</translation>
    </message>
    <message>
        <source>Settings saved for a particular device</source>
        <translation>Настройки, сохранённые под конкретное устройство</translation>
    </message>
    <message>
        <source>Save the current settings as a profile</source>
        <translation>Сохранить текущие настройки профилем</translation>
    </message>
    <message>
        <source>Delete this profile</source>
        <translation>Удалить этот профиль</translation>
    </message>
    <message>
        <source> samples</source>
        <translation> отсчётов</translation>
    </message>
    <message>
        <source>How many samples to keep. What part of them is on screen is set by scrolling and zooming the plot itself.</source>
        <translation>Сколько отсчётов хранить. Какая их часть на экране — решают прокрутка и масштаб самого графика.</translation>
    </message>
    <message>
        <source>Buffer</source>
        <translation>Буфер</translation>
    </message>
    <message>
        <source>Series</source>
        <translation>Ряд</translation>
    </message>
    <message>
        <source>Min</source>
        <translation>Мин</translation>
    </message>
    <message>
        <source>Max</source>
        <translation>Макс</translation>
    </message>
    <message>
        <source>Avg</source>
        <translation>Среднее</translation>
    </message>
    <message>
        <source>Click to show or hide, double-click to change the colour</source>
        <translation>Клик — показать или скрыть, двойной клик — сменить цвет</translation>
    </message>
    <message>
        <source>Double-click to rename</source>
        <translation>Двойной клик — переименовать</translation>
    </message>
    <message>
        <source>Series colour</source>
        <translation>Цвет ряда</translation>
    </message>
    <message>
        <source>Change colour…</source>
        <translation>Сменить цвет…</translation>
    </message>
    <message>
        <source>Rename…</source>
        <translation>Переименовать…</translation>
    </message>
    <message>
        <source>Change scale limits…</source>
        <translation>Изменить пределы шкалы…</translation>
    </message>
    <message>
        <source>Set scale limits…</source>
        <translation>Задать пределы шкалы…</translation>
    </message>
    <message>
        <source>Back to automatic scale</source>
        <translation>Вернуть автомасштаб</translation>
    </message>
    <message>
        <source>Clear this series only</source>
        <translation>Очистить только этот ряд</translation>
    </message>
    <message>
        <source>New profile</source>
        <translation>Новый профиль</translation>
    </message>
    <message>
        <source>Profile name:</source>
        <translation>Имя профиля:</translation>
    </message>
    <message>
        <source>That name cannot be used for a file</source>
        <translation>Такое имя не годится для файла</translation>
    </message>
    <message>
        <source>Could not save the profile</source>
        <translation>Не удалось сохранить профиль</translation>
    </message>
    <message>
        <source>Plotter profile: %1</source>
        <translation>Профиль плоттера: %1</translation>
    </message>
</context>
<context>
    <name>spotty::PlotterPlugin</name>
    <message>
        <source>Plotter</source>
        <translation>Плоттер</translation>
    </message>
    <message>
        <source>Spotty — plotter</source>
        <translation>Spotty — плоттер</translation>
    </message>
</context>
<context>
    <name>spotty::PluginManager</name>
    <message>
        <source>Not a Spotty plugin of any known kind.</source>
        <translation>Не плагин Spotty ни одного известного вида.</translation>
    </message>
    <message>
        <source>Not a Spotty interface plugin.</source>
        <translation>Это не плагин интерфейса Spotty.</translation>
    </message>
    <message>
        <source>Built against API version %1, this build expects %2.</source>
        <translation>Собран для версии API %1, эта сборка ожидает %2.</translation>
    </message>
    <message>
        <source>Plugin reports an empty id.</source>
        <translation>Плагин сообщает пустой идентификатор.</translation>
    </message>
    <message>
        <source>Another plugin already provides id &quot;%1&quot;.</source>
        <translation>Идентификатор «%1» уже занят другим плагином.</translation>
    </message>
</context>
<context>
    <name>spotty::ScaleLimitsDialog</name>
    <message>
        <source>Scale limits — %1</source>
        <translation>Пределы шкалы — %1</translation>
    </message>
    <message>
        <source>Minimum</source>
        <translation>Минимум</translation>
    </message>
    <message>
        <source>Maximum</source>
        <translation>Максимум</translation>
    </message>
    <message>
        <source>Measured: %1 … %2</source>
        <translation>Измерено: %1 … %2</translation>
    </message>
    <message>
        <source>Use measured</source>
        <translation>Взять измеренные</translation>
    </message>
</context>
<context>
    <name>spotty::SchemaForm</name>
    <message>
        <source>General</source>
        <translation>Общие</translation>
    </message>
</context>
<context>
    <name>spotty::SearchPanel</name>
    <message>
        <source>Search</source>
        <translation>Поиск</translation>
    </message>
    <message>
        <source>Find in output</source>
        <translation>Найти в выводе</translation>
    </message>
    <message>
        <source>Previous match</source>
        <translation>Предыдущее совпадение</translation>
    </message>
    <message>
        <source>Next match</source>
        <translation>Следующее совпадение</translation>
    </message>
    <message>
        <source>Regular expression</source>
        <translation>Регулярное выражение</translation>
    </message>
    <message>
        <source>Case sensitive</source>
        <translation>Учитывать регистр</translation>
    </message>
    <message>
        <source>Whole words</source>
        <translation>Слова целиком</translation>
    </message>
    <message>
        <source>Show only matching lines</source>
        <translation>Только совпавшие строки</translation>
    </message>
    <message>
        <source>Hides everything that does not match, instead of just highlighting it.</source>
        <translation>Скрывает всё несовпавшее, а не просто подсвечивает совпадения.</translation>
    </message>
    <message>
        <source>Highlight rules</source>
        <translation>Правила подсветки</translation>
    </message>
    <message>
        <source>Add rule</source>
        <translation>Добавить правило</translation>
    </message>
    <message>
        <source>Delete rule</source>
        <translation>Удалить правило</translation>
    </message>
    <message>
        <source>Add a highlight rule from the search pattern</source>
        <translation>Добавить правило подсветки из образца поиска</translation>
    </message>
    <message>
        <source>Find...</source>
        <translation>Найти…</translation>
    </message>
    <message>
        <source>Highlight colour</source>
        <translation>Цвет подсветки</translation>
    </message>
    <message>
        <source>For example: ^(WARN|ERROR).*[0-9]+$</source>
        <translation>Например: ^(WARN|ERROR).*[0-9]+$</translation>
    </message>
    <message>
        <source>%1 of %2</source>
        <translation>%1 из %2</translation>
    </message>
    <message numerus="yes">
        <source>%n line(s)</source>
        <translation>
            <numerusform>%n строка</numerusform>
            <numerusform>%n строки</numerusform>
            <numerusform>%n строк</numerusform>
        </translation>
    </message>
    <message>
        <source>Double-click to change</source>
        <translation>Двойной щелчок меняет цвет</translation>
    </message>
</context>
<context>
    <name>spotty::SearchPlugin</name>
    <message>
        <source>Search</source>
        <translation>Поиск</translation>
    </message>
</context>
<context>
    <name>spotty::SendBar</name>
    <message>
        <source>Data to send</source>
        <translation>Данные для отправки</translation>
    </message>
    <message>
        <source>How the entered text is interpreted</source>
        <translation>Как трактовать введённый текст</translation>
    </message>
    <message>
        <source>Appended to every message</source>
        <translation>Дописывается к каждой посылке</translation>
    </message>
    <message>
        <source>Send</source>
        <translation>Отправка</translation>
    </message>
    <message>
        <source>Open an interface to send data</source>
        <translation>Откройте интерфейс, чтобы отправлять данные</translation>
    </message>
    <message>
        <source>Send to</source>
        <translation>Отправить в</translation>
    </message>
    <message>
        <source>Interface A</source>
        <translation>Интерфейс A</translation>
    </message>
    <message>
        <source>Interface B</source>
        <translation>Интерфейс B</translation>
    </message>
    <message>
        <source>Both</source>
        <translation>Оба</translation>
    </message>
    <message>
        <source>First available</source>
        <translation>Первый доступный</translation>
    </message>
    <message>
        <source>Save as macro</source>
        <translation>Сохранить как макрос</translation>
    </message>
</context>
<context>
    <name>spotty::Session</name>
    <message>
        <source>Interface &quot;%1&quot; is not known.</source>
        <translation>Интерфейс «%1» неизвестен.</translation>
    </message>
    <message>
        <source>Plugin &quot;%1&quot; is not loaded.</source>
        <translation>Плагин «%1» не загружен.</translation>
    </message>
    <message>
        <source>Plugin &quot;%1&quot; could not create a channel.</source>
        <translation>Плагин «%1» не смог создать канал.</translation>
    </message>
    <message>
        <source>No interface selected.</source>
        <translation>Интерфейс не выбран.</translation>
    </message>
    <message>
        <source>Device is not present</source>
        <translation>Устройства нет в системе</translation>
    </message>
    <message>
        <source>--- %1 opened ---</source>
        <translation>--- %1 открыт ---</translation>
    </message>
    <message>
        <source>--- closed ---</source>
        <translation>--- закрыт ---</translation>
    </message>
    <message>
        <source>The interface is not open.</source>
        <translation>Интерфейс не открыт.</translation>
    </message>
    <message>
        <source>--- device is back, reopening ---</source>
        <translation>--- устройство вернулось, открываю заново ---</translation>
    </message>
    <message>
        <source>--- device disconnected ---</source>
        <translation>--- устройство отключено ---</translation>
    </message>
    <message>
        <source>Device disconnected</source>
        <translation>Устройство отключено</translation>
    </message>
</context>
<context>
    <name>spotty::SettingsDialog</name>
    <message>
        <source>Black</source>
        <translation>Чёрный</translation>
    </message>
    <message>
        <source>Red</source>
        <translation>Красный</translation>
    </message>
    <message>
        <source>Green</source>
        <translation>Зелёный</translation>
    </message>
    <message>
        <source>Yellow</source>
        <translation>Жёлтый</translation>
    </message>
    <message>
        <source>Blue</source>
        <translation>Синий</translation>
    </message>
    <message>
        <source>Magenta</source>
        <translation>Пурпурный</translation>
    </message>
    <message>
        <source>Cyan</source>
        <translation>Голубой</translation>
    </message>
    <message>
        <source>White</source>
        <translation>Белый</translation>
    </message>
    <message>
        <source>Bright black</source>
        <translation>Яркий чёрный</translation>
    </message>
    <message>
        <source>Bright red</source>
        <translation>Яркий красный</translation>
    </message>
    <message>
        <source>Bright green</source>
        <translation>Яркий зелёный</translation>
    </message>
    <message>
        <source>Bright yellow</source>
        <translation>Яркий жёлтый</translation>
    </message>
    <message>
        <source>Bright blue</source>
        <translation>Яркий синий</translation>
    </message>
    <message>
        <source>Bright magenta</source>
        <translation>Яркий пурпурный</translation>
    </message>
    <message>
        <source>Bright cyan</source>
        <translation>Яркий голубой</translation>
    </message>
    <message>
        <source>Bright white</source>
        <translation>Яркий белый</translation>
    </message>
    <message>
        <source>Open / close interface</source>
        <translation>Открыть или закрыть интерфейс</translation>
    </message>
    <message>
        <source>Clear terminal</source>
        <translation>Очистить терминал</translation>
    </message>
    <message>
        <source>Toggle hexadecimal dump</source>
        <translation>Переключить шестнадцатеричный дамп</translation>
    </message>
    <message>
        <source>Toggle timestamps</source>
        <translation>Переключить метки времени</translation>
    </message>
    <message>
        <source>Focus send bar</source>
        <translation>Фокус в строку отправки</translation>
    </message>
    <message>
        <source>Settings</source>
        <translation>Настройки</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Общие</translation>
    </message>
    <message>
        <source>Terminal</source>
        <translation>Терминал</translation>
    </message>
    <message>
        <source>Send</source>
        <translation>Отправка</translation>
    </message>
    <message>
        <source>Show / hide the side panel</source>
        <translation>Показать или скрыть боковую панель</translation>
    </message>
    <message>
        <source>Data</source>
        <translation>Данные</translation>
    </message>
    <message>
        <source>Shortcuts</source>
        <translation>Горячие клавиши</translation>
    </message>
    <message>
        <source>Interfaces</source>
        <translation>Интерфейсы</translation>
    </message>
    <message>
        <source>Plugins</source>
        <translation>Плагины</translation>
    </message>
    <message>
        <source>System</source>
        <translation>Системный</translation>
    </message>
    <message>
        <source>Language</source>
        <translation>Язык</translation>
    </message>
    <message>
        <source>Takes effect after restarting Spotty.</source>
        <translation>Вступит в силу после перезапуска Spotty.</translation>
    </message>
    <message>
        <source>Dark</source>
        <translation>Тёмная</translation>
    </message>
    <message>
        <source>Light</source>
        <translation>Светлая</translation>
    </message>
    <message>
        <source>Theme</source>
        <translation>Тема</translation>
    </message>
    <message>
        <source>Open the remembered interface on startup</source>
        <translation>Открывать запомненный интерфейс при запуске</translation>
    </message>
    <message>
        <source>Opening a port asserts DTR, which resets many boards, and takes the port away from any other program using it.</source>
        <translation>Открытие порта поднимает DTR, а у многих плат это сброс; порт при этом перехватывается у другой программы, которая им пользуется.</translation>
    </message>
    <message>
        <source>Only one running copy of Spotty</source>
        <translation>Только одна работающая копия Spotty</translation>
    </message>
    <message>
        <source>Starting Spotty again raises the existing window instead of opening a second one.</source>
        <translation>Повторный запуск покажет уже открытое окно вместо второго.</translation>
    </message>
    <message>
        <source>Reset</source>
        <translation>Сброс</translation>
    </message>
    <message>
        <source>Erases all settings, remembered interfaces and the send history. Cannot be undone.</source>
        <translation>Стирает все настройки, запомненные интерфейсы и историю отправки. Действие необратимо.</translation>
    </message>
    <message>
        <source>Reset everything to defaults…</source>
        <translation>Сбросить всё к умолчаниям…</translation>
    </message>
    <message>
        <source>Reset to defaults</source>
        <translation>Сброс к умолчаниям</translation>
    </message>
    <message>
        <source>This erases all settings, remembered interfaces and the send history, and cannot be undone.

Continue?</source>
        <translation>Это сотрёт все настройки, запомненные интерфейсы и историю отправки — действие необратимо.

Продолжить?</translation>
    </message>
    <message>
        <source>Font</source>
        <translation>Шрифт</translation>
    </message>
    <message>
        <source>Font size</source>
        <translation>Размер шрифта</translation>
    </message>
    <message>
        <source> lines</source>
        <translation> строк</translation>
    </message>
    <message>
        <source>Buffer size</source>
        <translation>Размер буфера</translation>
    </message>
    <message>
        <source>Received text encoding</source>
        <translation>Кодировка принимаемого текста</translation>
    </message>
    <message>
        <source>Bytes per hex row</source>
        <translation>Байт в строке дампа</translation>
    </message>
    <message>
        <source>Qt date/time format, for example HH:mm:ss.zzz</source>
        <translation>Формат даты и времени Qt, например HH:mm:ss.zzz</translation>
    </message>
    <message>
        <source>Timestamp format</source>
        <translation>Формат метки времени</translation>
    </message>
    <message>
        <source>Show timestamps</source>
        <translation>Показывать метки времени</translation>
    </message>
    <message>
        <source>Show time relative to the previous line</source>
        <translation>Показывать время относительно предыдущей строки</translation>
    </message>
    <message>
        <source>Show transmit and receive marks</source>
        <translation>Показывать метки приёма и передачи</translation>
    </message>
    <message>
        <source>Echo sent data into the terminal</source>
        <translation>Отражать отправленное в терминале</translation>
    </message>
    <message>
        <source>Lines made only of numbers separated by this character can be hidden from the terminal — right-click the toolbar button for quick presets. Hidden lines still reach the chart, the search and the log.</source>
        <translation>Строки из одних чисел через этот знак можно скрыть из терминала — быстрый выбор разделителя по правому клику на кнопке в панели. В график, поиск и журнал скрытые строки при этом попадают.</translation>
    </message>
    <message>
        <source>Telemetry delimiter</source>
        <translation>Разделитель телеметрии</translation>
    </message>
    <message>
        <source>ANSI colours</source>
        <translation>Цвета ANSI</translation>
    </message>
    <message>
        <source>ANSI colour</source>
        <translation>Цвет ANSI</translation>
    </message>
    <message>
        <source>Reset to theme colours</source>
        <translation>Вернуть цвета темы</translation>
    </message>
    <message>
        <source>Colours will follow the theme again.</source>
        <translation>Цвета снова будут следовать теме.</translation>
    </message>
    <message>
        <source>Default format</source>
        <translation>Формат по умолчанию</translation>
    </message>
    <message>
        <source>Default termination</source>
        <translation>Терминация по умолчанию</translation>
    </message>
    <message>
        <source> entries</source>
        <translation> записей</translation>
    </message>
    <message>
        <source>History size</source>
        <translation>Размер истории</translation>
    </message>
    <message>
        <source>History is kept in a plain text file next to the configuration and survives restarts.</source>
        <translation>История хранится текстовым файлом рядом с настройками и переживает перезапуск.</translation>
    </message>
    <message>
        <source>Stream (split on line breaks)</source>
        <translation>Потоком (по переводам строк)</translation>
    </message>
    <message>
        <source>Inter-byte timeout</source>
        <translation>По межбайтовой паузе</translation>
    </message>
    <message>
        <source>Delimiter</source>
        <translation>По разделителю</translation>
    </message>
    <message>
        <source>Fixed length</source>
        <translation>По фиксированной длине</translation>
    </message>
    <message>
        <source>Split incoming data by</source>
        <translation>Разбивать принимаемое</translation>
    </message>
    <message>
        <source> ms</source>
        <translation> мс</translation>
    </message>
    <message>
        <source>Gap</source>
        <translation>Пауза</translation>
    </message>
    <message>
        <source>Hexadecimal, for example 0A or 0D0A</source>
        <translation>Шестнадцатеричная запись, например 0A или 0D0A</translation>
    </message>
    <message>
        <source> bytes</source>
        <translation> байт</translation>
    </message>
    <message>
        <source>Message length</source>
        <translation>Длина сообщения</translation>
    </message>
    <message>
        <source>A binary stream without line breaks collapses into one endless line. Splitting by an inter-byte gap is how most binary protocols over UART frame their messages.</source>
        <translation>Двоичный поток без переводов строк слипается в одну бесконечную строку. Разбиение по межбайтовой паузе — то, как размечает сообщения большинство двоичных протоколов поверх UART.</translation>
    </message>
    <message>
        <source>Plugin</source>
        <translation>Плагин</translation>
    </message>
    <message>
        <source>Details</source>
        <translation>Подробности</translation>
    </message>
    <message>
        <source>Panels and data</source>
        <translation>Панели и обработка данных</translation>
    </message>
    <message>
        <source>Rejected</source>
        <translation>Отклонены</translation>
    </message>
    <message>
        <source>Searched directories</source>
        <translation>Просмотренные каталоги</translation>
    </message>
    <message>
        <source>None — plugins are built into the executable.</source>
        <translation>Нет — плагины вкомпилированы в исполняемый файл.</translation>
    </message>
    <message>
        <source>Macro shortcuts are assigned in the Macros panel, on each macro.</source>
        <translation>Горячие клавиши макросов задаются в панели макросов, у каждого макроса.</translation>
    </message>
</context>
<context>
    <name>spotty::SignalGenChannel</name>
    <message>
        <source>Virtual signal source</source>
        <translation>Виртуальный источник сигналов</translation>
    </message>
</context>
<context>
    <name>spotty::SignalGenPlugin</name>
    <message>
        <source>Virtual math signal source</source>
        <translation>Виртуальный источник математических сигналов</translation>
    </message>
    <message>
        <source>Waveform</source>
        <translation>Форма сигнала</translation>
    </message>
    <message>
        <source>Sine</source>
        <translation>Синус</translation>
    </message>
    <message>
        <source>Cosine</source>
        <translation>Косинус</translation>
    </message>
    <message>
        <source>Square</source>
        <translation>Меандр</translation>
    </message>
    <message>
        <source>Triangle</source>
        <translation>Треугольник</translation>
    </message>
    <message>
        <source>Sawtooth</source>
        <translation>Пила</translation>
    </message>
    <message>
        <source>Noise</source>
        <translation>Шум</translation>
    </message>
    <message>
        <source>Chirp (rising frequency)</source>
        <translation>Свип (растущая частота)</translation>
    </message>
    <message>
        <source>Damped sine (retriggered)</source>
        <translation>Затухающая синусоида (с перезапуском)</translation>
    </message>
    <message>
        <source>Pulse train</source>
        <translation>Импульсы</translation>
    </message>
    <message>
        <source>Staircase</source>
        <translation>Лестница</translation>
    </message>
    <message>
        <source>All waveforms (6 columns)</source>
        <translation>Все формы разом (6 колонок)</translation>
    </message>
    <message>
        <source>Growing column count</source>
        <translation>Растущее число колонок</translation>
    </message>
    <message>
        <source>&quot;All waveforms&quot; and &quot;Growing column count&quot; emit several columns at once - handy for testing the chart panel&apos;s series table, its colours, and how it reacts to a series appearing mid-stream.</source>
        <translation>«Все формы разом» и «Растущее число колонок» выдают несколько колонок сразу — удобно проверять таблицу рядов плоттера, её цвета и то, как он встречает колонку, появившуюся посреди потока.</translation>
    </message>
    <message>
        <source>Period</source>
        <translation>Период</translation>
    </message>
    <message>
        <source>ms</source>
        <translation>мс</translation>
    </message>
    <message>
        <source>Length of one cycle. Chirp and the retriggered sine use it as the starting period of a longer, repeating pattern.</source>
        <translation>Длительность одного цикла. Свип и затухающая синусоида берут её как начальный период более長ого повторяющегося узора.</translation>
    </message>
    <message>
        <source>Amplitude</source>
        <translation>Амплитуда</translation>
    </message>
    <message>
        <source>Offset</source>
        <translation>Смещение</translation>
    </message>
    <message>
        <source>Added to every value; shifts the curve up or down.</source>
        <translation>Прибавляется к каждому значению; поднимает или опускает кривую.</translation>
    </message>
    <message>
        <source>Random jitter layered on top of the waveform, as a percentage of the amplitude.</source>
        <translation>Случайный разброс поверх формы, в процентах от амплитуды.</translation>
    </message>
    <message>
        <source>Sample interval</source>
        <translation>Интервал отсчётов</translation>
    </message>
    <message>
        <source>Output</source>
        <translation>Вывод</translation>
    </message>
    <message>
        <source>How often a new line is emitted.</source>
        <translation>Как часто выдаётся новая строка.</translation>
    </message>
    <message>
        <source>Prepend time column</source>
        <translation>Первой колонкой — время</translation>
    </message>
    <message>
        <source>First column holds seconds elapsed since the channel opened - a steady reference axis, useful as a custom X axis in the chart panel.</source>
        <translation>Первая колонка — секунды с открытия канала: ровная опорная ось, годится как своя ось X в плоттере.</translation>
    </message>
    <message>
        <source>Emit status text lines</source>
        <translation>Слать текстовые строки состояния</translation>
    </message>
    <message>
        <source>Occasionally sends a non-numeric line, like a device mixing log messages into telemetry - checks that the chart skips it instead of breaking.</source>
        <translation>Изредка шлёт нечисловую строку, как устройство, мешающее сообщения с телеметрией, — проверяет, что плоттер её пропустит, а не сломается.</translation>
    </message>
    <message>
        <source>Signal generator</source>
        <translation>Генератор сигналов</translation>
    </message>
</context>
<context>
    <name>spotty::TerminalView</name>
    <message>
        <source>No lines match the filter</source>
        <translation>Ни одна строка не подходит под фильтр</translation>
    </message>
    <message>
        <source>Copy</source>
        <translation>Копировать</translation>
    </message>
    <message>
        <source>Select All</source>
        <translation>Выделить всё</translation>
    </message>
    <message>
        <source>Clear</source>
        <translation>Очистить</translation>
    </message>
    <message>
        <source>Line numbers</source>
        <translation>Номера строк</translation>
    </message>
    <message>
        <source>Count from Here</source>
        <translation>Считать отсюда</translation>
    </message>
    <message>
        <source>Scroll to Bottom</source>
        <translation>Прокрутить вниз</translation>
    </message>
</context>
<context>
    <name>spotty::ThemeManager</name>
    <message>
        <source>Undo</source>
        <translation>Отменить</translation>
    </message>
    <message>
        <source>Redo</source>
        <translation>Повторить</translation>
    </message>
    <message>
        <source>Cut</source>
        <translation>Вырезать</translation>
    </message>
    <message>
        <source>Copy</source>
        <translation>Копировать</translation>
    </message>
    <message>
        <source>Paste</source>
        <translation>Вставить</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>Удалить</translation>
    </message>
    <message>
        <source>Select All</source>
        <translation>Выделить всё</translation>
    </message>
</context>
<context>
    <name>spotty::UartChannel</name>
    <message>
        <source>Invalid baud rate.</source>
        <translation>Недопустимая скорость.</translation>
    </message>
    <message>
        <source>The port does not support %1 baud.</source>
        <translation>Порт не поддерживает скорость %1.</translation>
    </message>
</context>
<context>
    <name>spotty::UartPlugin</name>
    <message>
        <source>Port</source>
        <translation>Порт</translation>
    </message>
    <message>
        <source>Flow control</source>
        <translation>Управление потоком</translation>
    </message>
    <message>
        <source>Control lines</source>
        <translation>Линии управления</translation>
    </message>
    <message>
        <source>Baud rate</source>
        <translation>Скорость</translation>
    </message>
    <message>
        <source>Data bits</source>
        <translation>Бит данных</translation>
    </message>
    <message>
        <source>Parity</source>
        <translation>Чётность</translation>
    </message>
    <message>
        <source>None</source>
        <translation>Нет</translation>
    </message>
    <message>
        <source>Even</source>
        <translation>Чётная</translation>
    </message>
    <message>
        <source>Odd</source>
        <translation>Нечётная</translation>
    </message>
    <message>
        <source>Mark</source>
        <translation>Единица</translation>
    </message>
    <message>
        <source>Space</source>
        <translation>Ноль</translation>
    </message>
    <message>
        <source>Stop bits</source>
        <translation>Стоп-биты</translation>
    </message>
    <message>
        <source>Hardware (RTS/CTS)</source>
        <translation>Аппаратное (RTS/CTS)</translation>
    </message>
    <message>
        <source>Software (XON/XOFF)</source>
        <translation>Программное (XON/XOFF)</translation>
    </message>
    <message>
        <source>Assert DTR on open</source>
        <translation>Поднимать DTR при открытии</translation>
    </message>
    <message>
        <source>On many boards DTR is wired to reset - clear it to avoid rebooting the device when the port opens.</source>
        <translation>У многих плат DTR заведён на сброс — снимите флажок, чтобы открытие порта не перезагружало устройство.</translation>
    </message>
    <message>
        <source>Assert RTS on open</source>
        <translation>Поднимать RTS при открытии</translation>
    </message>
    <message>
        <source>Serial / UART</source>
        <translation>Последовательный порт / UART</translation>
    </message>
</context>
</TS>
