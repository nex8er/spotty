/**
 * \file spotty-uartd.c
 * \brief Агент проброса UART в raw TCP для транспорта `tcptunnel` программы Spotty.
 *
 * Один процесс обслуживает один последовательный порт и один TCP-порт. Всё, что приходит
 * из UART, получают все подключённые клиенты; всё, что присылает любой клиент, уходит в
 * UART. Никакого протокола поверх нет: подойдёт `nc`, `socat` или скрипт.
 *
 * \par Почему на C и без зависимостей
 *
 * Агент собирается статически под musl для x86_64, i386, arm64, armv7/armv6/armel и
 * лежит внутри Spotty готовым бинарником. Каждая зависимость умножалась бы на число
 * архитектур, а размер важен: бинарник заливается по ssh на плату, где места мало.
 * Из-за этого же весь агент — один файл: кросс-сборка это один вызов `zig cc`.
 *
 * \par Что агент делает с пропавшим UART
 *
 * Слушающий сокет живёт всегда, а вот клиентов, пока UART отсутствует (выдернули USB,
 * порт занят другой программой), агент не держит: уже подключённых он отключает, новых
 * принимает и сразу закрывает. Держать соединение открытым без порта значило бы показать
 * в Spotty живой канал, по которому никогда ничего не придёт.
 *
 * \par Чего агент не умеет намеренно
 *
 * Линий DTR/RTS/BREAK и смены скорости на лету: канал — чистый raw TCP, управляющей
 * части в нём нет. Параметры порта задаются конфигом при установке.
 *
 * \par Коды возврата
 *
 * 0 — штатная остановка или успешная проверка; 1 — ошибка во время работы (порт TCP занят);
 * 2 — ошибка аргументов или конфига.
 */
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef SPOTTY_UARTD_VERSION
#define SPOTTY_UARTD_VERSION "1.0.0"
#endif

/** \brief Сколько клиентов агент держит одновременно. Лишние отключаются сразу. */
#define MAX_CLIENTS 16

/** \brief Сколько правил белого списка помещается в конфиг. */
#define MAX_ALLOW 32

/** \brief За раз читается из клиента; столько же может застрять в очереди к UART. */
#define CLIENT_CHUNK 4096

/** \brief За раз читается из UART. */
#define UART_CHUNK 16384

/**
 * \brief Сколько непрочитанного агент копит для одного клиента, прежде чем отключить его.
 *
 * Запись в сокет не блокирует, и клиент, который не читает, иначе рос бы в памяти до тех
 * пор, пока не кончится она вся — на плате это единицы десятков мегабайт. Отключение
 * жёсткое: мягкое ждало бы, пока он прочтёт накопленное, то есть то, чего он не делает.
 */
#define CLIENT_BACKLOG_LIMIT (1u << 20)

/** \brief Пауза между попытками открыть пропавший UART, мс. */
#define UART_RETRY_MS 1000

/** \brief Правило белого списка: адрес и длина префикса в битах. */
typedef struct {
    int family; ///< AF_INET или AF_INET6.
    unsigned char addr[16];
    int prefix;
} allow_rule_t;

/** \brief Параметры одного экземпляра агента. */
typedef struct {
    char device[PATH_MAX];
    long baud;
    int databits;
    char parity; ///< 'N', 'E' или 'O'.
    int stopbits;
    int flow; ///< 0 — нет, 1 — RTS/CTS, 2 — XON/XOFF.
    int port;
    char bind[128];
    allow_rule_t allow[MAX_ALLOW];
    int allowCount;
} config_t;

/** \brief Один подключённый клиент и очередь не ушедшего ему. */
typedef struct {
    int fd;
    char addr[64];
    unsigned char *out;
    size_t outLen;
    size_t outCap;
} client_t;

static volatile sig_atomic_t g_stop = 0;

static client_t g_clients[MAX_CLIENTS];
static int g_clientCount = 0;
static int g_uart = -1;
static int g_listen = -1;

/** \brief Принятое от клиентов, но ещё не записанное в UART (она не успела принять). */
static unsigned char g_pending[CLIENT_CHUNK];
static size_t g_pendingLen = 0;

/** \brief Записать строку в журнал (stderr, который systemd отдаёт journald). */
static void logmsg(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fputs("spotty-uartd: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

static void onSignal(int sig)
{
    (void) sig;
    g_stop = 1;
}

/** \brief Монотонное время в миллисекундах: перевод системных часов не должен ломать паузы. */
static long long nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ------------------------------------------------------------------------------------------
 * Разбор настроек
 * ---------------------------------------------------------------------------------------- */

static void configDefaults(config_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->baud = 115200;
    cfg->databits = 8;
    cfg->parity = 'N';
    cfg->stopbits = 1;
    cfg->port = 7777;
    snprintf(cfg->bind, sizeof cfg->bind, "0.0.0.0");
}

/**
 * \brief Константа скорости termios для числа бод.
 * \return Константу или 0, если скорость не поддерживается.
 *
 * Список закрыт намеренно: нестандартные скорости на Linux требуют termios2 с `BOTHER`, а
 * его заголовки конфликтуют с обычными. Все распространённые скорости до 4 Мбод покрыты.
 */
static speed_t baudConstant(long baud)
{
    switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B500000
    case 500000: return B500000;
#endif
#ifdef B576000
    case 576000: return B576000;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
#ifdef B1000000
    case 1000000: return B1000000;
#endif
#ifdef B1152000
    case 1152000: return B1152000;
#endif
#ifdef B1500000
    case 1500000: return B1500000;
#endif
#ifdef B2000000
    case 2000000: return B2000000;
#endif
#ifdef B2500000
    case 2500000: return B2500000;
#endif
#ifdef B3000000
    case 3000000: return B3000000;
#endif
#ifdef B3500000
    case 3500000: return B3500000;
#endif
#ifdef B4000000
    case 4000000: return B4000000;
#endif
    default: break;
    }
#ifdef __APPLE__
    /* На macOS константа скорости — само число бод, и любое значение допустимо. */
    if (baud > 0)
        return (speed_t) baud;
#endif
    return 0;
}

/**
 * \brief Разобрать одно правило белого списка: `192.168.1.5`, `10.0.0.0/8`, `fe80::/10`.
 * \return 0 при успехе, -1 при неверной записи.
 */
static int parseAllowRule(const char *text, allow_rule_t *rule)
{
    char buf[96];
    if (strlen(text) >= sizeof buf)
        return -1;
    strcpy(buf, text);

    int prefix = -1;
    char *slash = strchr(buf, '/');
    if (slash) {
        *slash = '\0';
        char *end = NULL;
        long value = strtol(slash + 1, &end, 10);
        if (end == slash + 1 || *end != '\0' || value < 0 || value > 128)
            return -1;
        prefix = (int) value;
    }

    memset(rule, 0, sizeof *rule);
    if (inet_pton(AF_INET, buf, rule->addr) == 1) {
        rule->family = AF_INET;
        if (prefix < 0)
            prefix = 32;
        if (prefix > 32)
            return -1;
    } else if (inet_pton(AF_INET6, buf, rule->addr) == 1) {
        rule->family = AF_INET6;
        if (prefix < 0)
            prefix = 128;
    } else {
        return -1;
    }
    rule->prefix = prefix;
    return 0;
}

/** \brief Разобрать список правил через запятую или пробел. */
static int parseAllowList(config_t *cfg, const char *list, char *err, size_t errSize)
{
    char buf[1024];
    if (strlen(list) >= sizeof buf) {
        snprintf(err, errSize, "allow list is too long");
        return -1;
    }
    strcpy(buf, list);

    cfg->allowCount = 0;
    for (char *token = strtok(buf, ", \t"); token; token = strtok(NULL, ", \t")) {
        if (cfg->allowCount >= MAX_ALLOW) {
            snprintf(err, errSize, "too many allow rules (at most %d)", MAX_ALLOW);
            return -1;
        }
        if (parseAllowRule(token, &cfg->allow[cfg->allowCount]) != 0) {
            snprintf(err, errSize, "bad allow rule '%s'", token);
            return -1;
        }
        ++cfg->allowCount;
    }
    return 0;
}

/** \brief Разобрать `8N1`, `7E2` и подобные. */
static int parseFormat(config_t *cfg, const char *text, char *err, size_t errSize)
{
    if (strlen(text) != 3 || text[0] < '5' || text[0] > '8') {
        snprintf(err, errSize, "bad format '%s' (expected like 8N1)", text);
        return -1;
    }
    char parity = text[1];
    if (parity >= 'a' && parity <= 'z')
        parity = (char) (parity - 'a' + 'A');
    if ((parity != 'N' && parity != 'E' && parity != 'O') || (text[2] != '1' && text[2] != '2')) {
        snprintf(err, errSize, "bad format '%s' (expected like 8N1)", text);
        return -1;
    }
    cfg->databits = text[0] - '0';
    cfg->parity = parity;
    cfg->stopbits = text[2] - '0';
    return 0;
}

/**
 * \brief Применить один параметр `ключ = значение`.
 *
 * Общий для файла и командной строки: `--baud 9600` и `baud=9600` — одно и то же. Неизвестный
 * ключ — ошибка, а не молчаливый пропуск: опечатка в конфиге иначе означала бы работу на
 * умолчаниях, и заметить это по поведению нельзя.
 */
static int setOption(config_t *cfg, const char *key, const char *value, char *err, size_t errSize)
{
    char *end = NULL;
    if (strcmp(key, "device") == 0) {
        if (strlen(value) >= sizeof cfg->device) {
            snprintf(err, errSize, "device path is too long");
            return -1;
        }
        strcpy(cfg->device, value);
    } else if (strcmp(key, "baud") == 0) {
        long baud = strtol(value, &end, 10);
        if (end == value || *end != '\0' || baudConstant(baud) == 0) {
            snprintf(err, errSize, "unsupported baud rate '%s'", value);
            return -1;
        }
        cfg->baud = baud;
    } else if (strcmp(key, "format") == 0) {
        return parseFormat(cfg, value, err, errSize);
    } else if (strcmp(key, "flow") == 0) {
        if (strcmp(value, "none") == 0)
            cfg->flow = 0;
        else if (strcmp(value, "rtscts") == 0)
            cfg->flow = 1;
        else if (strcmp(value, "xonxoff") == 0)
            cfg->flow = 2;
        else {
            snprintf(err, errSize, "bad flow '%s' (none, rtscts or xonxoff)", value);
            return -1;
        }
    } else if (strcmp(key, "port") == 0) {
        long port = strtol(value, &end, 10);
        if (end == value || *end != '\0' || port < 1 || port > 65535) {
            snprintf(err, errSize, "bad port '%s'", value);
            return -1;
        }
        cfg->port = (int) port;
    } else if (strcmp(key, "bind") == 0) {
        if (strlen(value) >= sizeof cfg->bind) {
            snprintf(err, errSize, "bind address is too long");
            return -1;
        }
        strcpy(cfg->bind, value);
    } else if (strcmp(key, "allow") == 0) {
        return parseAllowList(cfg, value, err, errSize);
    } else {
        snprintf(err, errSize, "unknown option '%s'", key);
        return -1;
    }
    return 0;
}

/** \brief Убрать пробелы и перевод строки с обоих концов (на месте). */
static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t')
        ++text;
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\n'
                       || text[len - 1] == '\r'))
        text[--len] = '\0';
    return text;
}

/** \brief Прочитать файл `ключ=значение`; `#` и `;` начинают комментарий-строку. */
static int loadConfigFile(config_t *cfg, const char *path, char *err, size_t errSize)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        snprintf(err, errSize, "cannot read %s: %s", path, strerror(errno));
        return -1;
    }

    char line[1200];
    int lineNo = 0;
    while (fgets(line, sizeof line, file)) {
        ++lineNo;
        char *text = trim(line);
        if (*text == '\0' || *text == '#' || *text == ';')
            continue;
        char *equals = strchr(text, '=');
        if (!equals) {
            snprintf(err, errSize, "%s:%d: expected key=value", path, lineNo);
            fclose(file);
            return -1;
        }
        *equals = '\0';
        char *key = trim(text);
        char *value = trim(equals + 1);

        char detail[256];
        if (setOption(cfg, key, value, detail, sizeof detail) != 0) {
            snprintf(err, errSize, "%s:%d: %s", path, lineNo, detail);
            fclose(file);
            return -1;
        }
    }
    fclose(file);
    return 0;
}

/* ------------------------------------------------------------------------------------------
 * Список последовательных портов (--scan)
 * ---------------------------------------------------------------------------------------- */

/** \brief Прочитать первую строку файла в буфер; пустая строка, если файла нет. */
static void readSmallFile(const char *path, char *out, size_t size)
{
    out[0] = '\0';
    FILE *file = fopen(path, "r");
    if (!file)
        return;
    if (fgets(out, (int) size, file))
        out[strcspn(out, "\r\n")] = '\0';
    fclose(file);
}

/** \brief Имя вида `tty0`…`tty63`: виртуальная консоль, а не последовательный порт. */
static int isVirtualConsole(const char *name)
{
    if (strncmp(name, "tty", 3) != 0 || name[3] == '\0')
        return 0;
    for (const char *p = name + 3; *p; ++p) {
        if (*p < '0' || *p > '9')
            return 0;
    }
    return 1;
}

/**
 * \brief Найти устойчивое имя порта в `/dev/serial/by-id`.
 *
 * Номер ttyUSB переназначается при каждом переподключении, а имя по идентификатору
 * привязано к самому адаптеру. В конфиг стоит класть именно его.
 */
static void findStablePath(const char *name, char *out, size_t size)
{
    out[0] = '\0';
    DIR *dir = opendir("/dev/serial/by-id");
    if (!dir)
        return;

    char target[PATH_MAX];
    snprintf(target, sizeof target, "/dev/%s", name);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;
        char link[PATH_MAX];
        char resolved[PATH_MAX];
        snprintf(link, sizeof link, "/dev/serial/by-id/%s", entry->d_name);
        if (realpath(link, resolved) && strcmp(resolved, target) == 0) {
            snprintf(out, size, "%s", link);
            break;
        }
    }
    closedir(dir);
}

/**
 * \brief Описание устройства: для USB — изготовитель, изделие и VID:PID, иначе имя драйвера.
 *
 * Идентификаторы лежат на несколько уровней выше узла порта (порт — интерфейс, а
 * `idVendor` у самого USB-устройства), поэтому поднимаемся по каталогам sysfs.
 */
static void describePort(const char *name, char *out, size_t size)
{
    out[0] = '\0';
    char link[PATH_MAX];
    char dir[PATH_MAX];
    snprintf(link, sizeof link, "/sys/class/tty/%s/device", name);
    if (!realpath(link, dir))
        return;

    for (int level = 0; level < 6; ++level) {
        char path[PATH_MAX + 32];
        char vendor[16];
        snprintf(path, sizeof path, "%s/idVendor", dir);
        readSmallFile(path, vendor, sizeof vendor);
        if (vendor[0]) {
            char product[16];
            char manufacturer[96];
            char title[96];
            snprintf(path, sizeof path, "%s/idProduct", dir);
            readSmallFile(path, product, sizeof product);
            snprintf(path, sizeof path, "%s/manufacturer", dir);
            readSmallFile(path, manufacturer, sizeof manufacturer);
            snprintf(path, sizeof path, "%s/product", dir);
            readSmallFile(path, title, sizeof title);
            snprintf(out, size, "%s%s%s (%s:%s)", manufacturer, manufacturer[0] ? " " : "", title,
                     vendor, product);
            return;
        }
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir)
            break;
        *slash = '\0';
    }

    /* Не USB: покажем хотя бы драйвер, чтобы отличить встроенный UART платы от остального. */
    snprintf(link, sizeof link, "/sys/class/tty/%s/device/driver", name);
    char driver[PATH_MAX];
    ssize_t len = readlink(link, driver, sizeof driver - 1);
    if (len > 0) {
        driver[len] = '\0';
        const char *base = strrchr(driver, '/');
        snprintf(out, size, "%s", base ? base + 1 : driver);
    }
}

/**
 * \brief Напечатать настоящие последовательные порты машины.
 *
 * Формат — строки из трёх полей через табуляцию: `устройство`, `устойчивое имя` (пусто,
 * если нет), `описание` (может быть пустым). Spotty разбирает его, не выбрасывая пустые
 * поля.
 *
 * Отсев — по sysfs, а не по маске имени. Виртуальные консоли `tty0…tty63` и `ttyS0…ttyS31`
 * без железа в `/dev` присутствуют всегда, и показывать их значило бы завалить список
 * нерабочими пунктами: у порта без микросхемы `type` равен 0 или нет ссылки `device`.
 */
static int scanPorts(void)
{
    DIR *dir = opendir("/sys/class/tty");
    if (!dir)
        return 0; /* не Linux или нет sysfs: «ничего не найдено» — не ошибка */

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (name[0] == '.' || isVirtualConsole(name))
            continue;
        if (strcmp(name, "console") == 0 || strcmp(name, "ptmx") == 0
            || strncmp(name, "ttyprintk", 9) == 0)
            continue;

        char path[PATH_MAX];
        snprintf(path, sizeof path, "/sys/class/tty/%s/device", name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;

        snprintf(path, sizeof path, "/sys/class/tty/%s/type", name);
        char type[16];
        readSmallFile(path, type, sizeof type);
        if (type[0] && atoi(type) == 0)
            continue; /* ядро завело узел, но микросхемы за ним нет */

        char stable[PATH_MAX];
        char description[256];
        findStablePath(name, stable, sizeof stable);
        describePort(name, description, sizeof description);
        printf("/dev/%s\t%s\t%s\n", name, stable, description);
    }
    closedir(dir);
    return 0;
}

/* ------------------------------------------------------------------------------------------
 * UART
 * ---------------------------------------------------------------------------------------- */

/**
 * \brief Открыть и настроить последовательный порт.
 * \return Дескриптор или -1; причина — в `err`.
 */
static int serialOpen(const config_t *cfg, char *err, size_t errSize)
{
    int fd = open(cfg->device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        snprintf(err, errSize, "cannot open %s: %s", cfg->device, strerror(errno));
        return -1;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        snprintf(err, errSize, "%s is not a serial port: %s", cfg->device, strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    tio.c_cflag &= (tcflag_t) ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
    switch (cfg->databits) {
    case 5: tio.c_cflag |= CS5; break;
    case 6: tio.c_cflag |= CS6; break;
    case 7: tio.c_cflag |= CS7; break;
    default: tio.c_cflag |= CS8; break;
    }
    if (cfg->parity != 'N') {
        tio.c_cflag |= PARENB;
        if (cfg->parity == 'O')
            tio.c_cflag |= PARODD;
    }
    if (cfg->stopbits == 2)
        tio.c_cflag |= CSTOPB;

    /* CLOCAL: без линии DCD порт не должен считаться «положившим трубку». HUPCL снят, чтобы
     * закрытие агентом не роняло DTR — на части плат это сброс. */
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= (tcflag_t) ~HUPCL;

    if (cfg->flow == 1)
        tio.c_cflag |= CRTSCTS;
    else if (cfg->flow == 2)
        tio.c_iflag |= IXON | IXOFF;

    /* VMIN=1: с O_NONBLOCK пустой read даёт EAGAIN. При VMIN=0 он давал бы 0 — тот же
     * результат, что и конец файла, и отличить «нет данных» от «порт пропал» было бы нельзя. */
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

    speed_t speed = baudConstant(cfg->baud);
    if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0
        || tcsetattr(fd, TCSANOW, &tio) != 0) {
        snprintf(err, errSize, "cannot configure %s: %s", cfg->device, strerror(errno));
        close(fd);
        return -1;
    }

#ifdef TIOCEXCL
    /* Эксклюзивный доступ: другая программа, открывшая порт одновременно с нами, портила бы
     * поток обоим. Сбой не страшен (на псевдотерминале он обычное дело). */
    (void) ioctl(fd, TIOCEXCL);
#endif
    tcflush(fd, TCIOFLUSH);
    (void) fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

/* ------------------------------------------------------------------------------------------
 * TCP
 * ---------------------------------------------------------------------------------------- */

static void setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        (void) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    (void) fcntl(fd, F_SETFD, FD_CLOEXEC);
}

/**
 * \brief Создать слушающий сокет.
 * \return Дескриптор или -1; причина — в `err`.
 */
static int listenOpen(const config_t *cfg, char *err, size_t errSize)
{
    char service[16];
    snprintf(service, sizeof service, "%d", cfg->port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *info = NULL;
    int rc = getaddrinfo(cfg->bind, service, &hints, &info);
    if (rc != 0 || !info) {
        snprintf(err, errSize, "bad bind address '%s': %s", cfg->bind, gai_strerror(rc));
        return -1;
    }

    int fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (fd < 0) {
        snprintf(err, errSize, "cannot create a socket: %s", strerror(errno));
        freeaddrinfo(info);
        return -1;
    }

    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#ifdef IPV6_V6ONLY
    if (info->ai_family == AF_INET6) {
        /* «::» должен принимать и IPv4 тоже: иначе «слушать везде» оставляло бы без порта
         * половину сети. */
        int off = 0;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    }
#endif

    if (bind(fd, info->ai_addr, info->ai_addrlen) != 0 || listen(fd, 8) != 0) {
        snprintf(err, errSize, "cannot listen on %s:%d: %s", cfg->bind, cfg->port,
                 strerror(errno));
        close(fd);
        freeaddrinfo(info);
        return -1;
    }
    freeaddrinfo(info);
    setNonBlocking(fd);
    return fd;
}

/** \brief Привести адрес партнёра к байтам; IPv4 внутри IPv6 (`::ffff:a.b.c.d`) становится IPv4. */
static void peerBytes(const struct sockaddr_storage *ss, int *family, unsigned char *bytes)
{
    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in *in4 = (const struct sockaddr_in *) ss;
        *family = AF_INET;
        memcpy(bytes, &in4->sin_addr, 4);
        return;
    }
    const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *) ss;
    if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {
        *family = AF_INET;
        memcpy(bytes, &in6->sin6_addr.s6_addr[12], 4);
    } else {
        *family = AF_INET6;
        memcpy(bytes, &in6->sin6_addr, 16);
    }
}

/** \brief Совпадает ли адрес с правилом по первым `prefix` битам. */
static int ruleMatches(const allow_rule_t *rule, int family, const unsigned char *bytes)
{
    if (rule->family != family)
        return 0;
    int whole = rule->prefix / 8;
    int rest = rule->prefix % 8;
    if (whole > 0 && memcmp(rule->addr, bytes, (size_t) whole) != 0)
        return 0;
    if (rest > 0) {
        unsigned char mask = (unsigned char) (0xFF << (8 - rest));
        if ((rule->addr[whole] & mask) != (bytes[whole] & mask))
            return 0;
    }
    return 1;
}

/** \brief Пускать ли клиента. Пустой белый список пускает всех. */
static int isAllowed(const config_t *cfg, const struct sockaddr_storage *ss)
{
    if (cfg->allowCount == 0)
        return 1;
    int family;
    unsigned char bytes[16];
    peerBytes(ss, &family, bytes);
    for (int i = 0; i < cfg->allowCount; ++i) {
        if (ruleMatches(&cfg->allow[i], family, bytes))
            return 1;
    }
    return 0;
}

/** \brief Адрес партнёра в виде строки для журнала. */
static void peerText(const struct sockaddr_storage *ss, char *out, size_t size)
{
    int family;
    unsigned char bytes[16];
    peerBytes(ss, &family, bytes);
    if (!inet_ntop(family, bytes, out, (socklen_t) size))
        snprintf(out, size, "?");
}

static void clientClose(int index, const char *reason)
{
    client_t *client = &g_clients[index];
    logmsg("client %s %s (%d left)", client->addr, reason, g_clientCount - 1);
    close(client->fd);
    free(client->out);
    g_clients[index] = g_clients[g_clientCount - 1];
    --g_clientCount;
}

static void clientsCloseAll(const char *reason)
{
    while (g_clientCount > 0)
        clientClose(g_clientCount - 1, reason);
}

static void keepAliveOn(int fd)
{
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    /* Оборванный без FIN кабель иначе держал бы клиента вечно, а мест всего MAX_CLIENTS:
     * проба через 30 с тишины, три неудачи подряд с шагом 10 с — около минуты. */
#ifdef TCP_KEEPIDLE
    int idle = 30;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
#elif defined(TCP_KEEPALIVE)
    int idle = 30;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof idle);
#endif
#ifdef TCP_KEEPINTVL
    int interval = 10;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof interval);
#endif
#ifdef TCP_KEEPCNT
    int count = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof count);
#endif
}

/** \brief Принять ожидающее соединение: проверить, пускать ли, и завести клиента. */
static void acceptClient(const config_t *cfg)
{
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    int fd = accept(g_listen, (struct sockaddr *) &ss, &len);
    if (fd < 0)
        return;

    char addr[64];
    peerText(&ss, addr, sizeof addr);

    if (g_uart < 0) {
        logmsg("rejected %s (serial port is not available)", addr);
        close(fd);
        return;
    }
    if (!isAllowed(cfg, &ss)) {
        logmsg("rejected %s (not in the allow list)", addr);
        close(fd);
        return;
    }
    if (g_clientCount >= MAX_CLIENTS) {
        logmsg("rejected %s (too many clients)", addr);
        close(fd);
        return;
    }

    setNonBlocking(fd);
    keepAliveOn(fd);

    client_t *client = &g_clients[g_clientCount++];
    memset(client, 0, sizeof *client);
    client->fd = fd;
    snprintf(client->addr, sizeof client->addr, "%s", addr);
    logmsg("client %s connected (%d total)", addr, g_clientCount);
}

/** \brief Дописать байты в очередь клиента. \return -1, если очередь переполнена. */
static int clientEnqueue(client_t *client, const unsigned char *data, size_t size)
{
    if (client->outLen + size > CLIENT_BACKLOG_LIMIT)
        return -1;
    if (client->outLen + size > client->outCap) {
        size_t cap = client->outCap ? client->outCap : 4096;
        while (cap < client->outLen + size)
            cap *= 2;
        unsigned char *grown = realloc(client->out, cap);
        if (!grown)
            return -1;
        client->out = grown;
        client->outCap = cap;
    }
    memcpy(client->out + client->outLen, data, size);
    client->outLen += size;
    return 0;
}

/** \brief Записать клиенту сколько примет сокет. \return -1 при ошибке соединения. */
static int clientFlush(client_t *client)
{
    while (client->outLen > 0) {
        ssize_t written = write(client->fd, client->out, client->outLen);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            return -1;
        }
        memmove(client->out, client->out + written, client->outLen - (size_t) written);
        client->outLen -= (size_t) written;
    }
    return 0;
}

/** \brief Раздать принятое из UART всем клиентам. */
static void broadcast(const unsigned char *data, size_t size)
{
    for (int i = g_clientCount - 1; i >= 0; --i) {
        client_t *client = &g_clients[i];
        /* Сперва то, что уже стоит в очереди: порядок байт внутри потока клиента нельзя менять. */
        if (clientEnqueue(client, data, size) != 0) {
            clientClose(i, "dropped (not reading, backlog limit reached)");
            continue;
        }
        if (clientFlush(client) != 0)
            clientClose(i, "disconnected");
    }
}

/* ------------------------------------------------------------------------------------------
 * Обмен
 * ---------------------------------------------------------------------------------------- */

static void uartClose(const char *reason)
{
    if (g_uart >= 0) {
        close(g_uart);
        g_uart = -1;
    }
    g_pendingLen = 0;
    clientsCloseAll(reason);
}

/** \brief Дописать в UART из очереди. \return -1, если порт пропал. */
static int pendingFlush(void)
{
    while (g_pendingLen > 0) {
        ssize_t written = write(g_uart, g_pending, g_pendingLen);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            return -1;
        }
        memmove(g_pending, g_pending + written, g_pendingLen - (size_t) written);
        g_pendingLen -= (size_t) written;
    }
    return 0;
}

/** \brief Прочитать у клиента и передать в UART; остаток при переполнении встаёт в очередь. */
static int clientRead(int index)
{
    unsigned char buf[CLIENT_CHUNK];
    ssize_t got = read(g_clients[index].fd, buf, sizeof buf);
    if (got == 0)
        return -1;
    if (got < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;

    size_t offset = 0;
    while (offset < (size_t) got) {
        ssize_t written = write(g_uart, buf + offset, (size_t) got - offset);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return -2; /* порт пропал */
        }
        offset += (size_t) written;
    }
    if (offset < (size_t) got) {
        /* g_pending пуст, пока читаем (клиенты не опрашиваются при непустой очереди), а
         * CLIENT_CHUNK не меньше прочитанного, поэтому остаток помещается всегда. */
        memcpy(g_pending, buf + offset, (size_t) got - offset);
        g_pendingLen = (size_t) got - offset;
    }
    return 0;
}

static int writePidFile(const char *path)
{
    FILE *file = fopen(path, "w");
    if (!file)
        return -1;
    fprintf(file, "%ld\n", (long) getpid());
    fclose(file);
    return 0;
}

static void usage(FILE *out)
{
    fputs("Usage: spotty-uartd [--config FILE] [options]\n"
          "\n"
          "Forwards a serial port to raw TCP. Everything read from the UART goes to every\n"
          "connected client; everything a client sends goes to the UART.\n"
          "\n"
          "Options (the same names work as key=value lines in the config file):\n"
          "  --device PATH     serial port, e.g. /dev/ttyUSB0 (required)\n"
          "  --baud N          default 115200\n"
          "  --format 8N1      data bits, parity (N/E/O), stop bits; default 8N1\n"
          "  --flow MODE       none (default), rtscts or xonxoff\n"
          "  --port N          TCP port, default 7777\n"
          "  --bind ADDR       listen address, default 0.0.0.0 (use :: for IPv6 too)\n"
          "  --allow LIST      only these clients may connect: addresses or CIDR, comma-\n"
          "                    separated. Empty (the default) lets everyone in\n"
          "\n"
          "Other:\n"
          "  --config FILE     read options from FILE; the command line overrides it\n"
          "  --pidfile FILE    write the process id to FILE\n"
          "  --check           validate the options and exit\n"
          "  --scan            list real serial ports (device, stable name, description)\n"
          "  --version\n",
          out);
}

int main(int argc, char **argv)
{
    config_t cfg;
    configDefaults(&cfg);

    char err[512];
    const char *configPath = NULL;
    const char *pidPath = NULL;
    int checkOnly = 0;

    /* Первый проход: найти --config, чтобы командная строка потом перекрыла файл, а не
     * наоборот, в каком бы порядке ни стояли аргументы. */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            configPath = argv[++i];
        else if (strncmp(argv[i], "--config=", 9) == 0)
            configPath = argv[i] + 9;
    }
    if (configPath && loadConfigFile(&cfg, configPath, err, sizeof err) != 0) {
        logmsg("%s", err);
        return 2;
    }

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("spotty-uartd %s\n", SPOTTY_UARTD_VERSION);
            return 0;
        }
        if (strcmp(arg, "--scan") == 0)
            return scanPorts();
        if (strcmp(arg, "--check") == 0) {
            checkOnly = 1;
            continue;
        }
        if (strncmp(arg, "--", 2) != 0) {
            logmsg("unexpected argument '%s'", arg);
            usage(stderr);
            return 2;
        }

        char name[32];
        const char *value = NULL;
        const char *equals = strchr(arg + 2, '=');
        if (equals) {
            size_t len = (size_t) (equals - (arg + 2));
            if (len >= sizeof name)
                len = sizeof name - 1;
            memcpy(name, arg + 2, len);
            name[len] = '\0';
            value = equals + 1;
        } else {
            snprintf(name, sizeof name, "%s", arg + 2);
        }

        if (strcmp(name, "config") == 0) {
            if (!equals)
                ++i; /* значение уже учтено первым проходом */
            continue;
        }
        if (!value) {
            if (i + 1 >= argc) {
                logmsg("option '%s' needs a value", arg);
                return 2;
            }
            value = argv[++i];
        }
        if (strcmp(name, "pidfile") == 0) {
            pidPath = value;
            continue;
        }
        if (setOption(&cfg, name, value, err, sizeof err) != 0) {
            logmsg("%s", err);
            return 2;
        }
    }

    if (cfg.device[0] == '\0') {
        logmsg("no serial port given (--device)");
        return 2;
    }
    if (checkOnly)
        return 0;

    signal(SIGPIPE, SIG_IGN);
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = onSignal; /* без SA_RESTART: poll() должен вернуться по сигналу */
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);

    setvbuf(stderr, NULL, _IOLBF, 0);

    /* Порт TCP занимаем сразу, ещё до UART: занятый порт — ошибка настройки, и узнать о ней
     * нужно при запуске, а не когда USB-адаптер наконец появится. */
    g_listen = listenOpen(&cfg, err, sizeof err);
    if (g_listen < 0) {
        logmsg("%s", err);
        return 1;
    }
    if (pidPath && writePidFile(pidPath) != 0)
        logmsg("cannot write %s: %s", pidPath, strerror(errno));

    logmsg("%s %ld %d%c%d -> %s:%d%s", cfg.device, cfg.baud, cfg.databits, cfg.parity,
           cfg.stopbits, cfg.bind, cfg.port, cfg.allowCount ? " (allow list on)" : "");
    logmsg("listening");

    long long nextOpen = 0;
    int openFailureLogged = 0;
    unsigned char buf[UART_CHUNK];

    while (!g_stop) {
        if (g_uart < 0 && nowMs() >= nextOpen) {
            g_uart = serialOpen(&cfg, err, sizeof err);
            if (g_uart >= 0) {
                logmsg("serial port opened");
                openFailureLogged = 0;
            } else {
                /* Писать в журнал каждую секунду одно и то же незачем: достаточно первого раза. */
                if (!openFailureLogged)
                    logmsg("%s (retrying)", err);
                openFailureLogged = 1;
                nextOpen = nowMs() + UART_RETRY_MS;
            }
        }

        struct pollfd fds[2 + MAX_CLIENTS];
        fds[0].fd = g_listen;
        fds[0].events = POLLIN;
        fds[1].fd = g_uart; /* отрицательный дескриптор poll() пропускает */
        fds[1].events = POLLIN | (g_pendingLen > 0 ? POLLOUT : 0);
        for (int i = 0; i < g_clientCount; ++i) {
            fds[2 + i].fd = g_clients[i].fd;
            /* Пока очередь к UART не пуста, у клиентов читать нельзя — иначе очередь
             * росла бы без предела. Поток при этом замирает на стороне TCP, как и должно
             * быть при медленном порте. */
            fds[2 + i].events = (g_pendingLen == 0 ? POLLIN : 0)
                                | (g_clients[i].outLen > 0 ? POLLOUT : 0);
        }
        for (int i = g_clientCount; i < MAX_CLIENTS; ++i)
            fds[2 + i].fd = -1;

        int timeout = g_uart < 0 ? UART_RETRY_MS / 2 : 1000;
        int ready = poll(fds, (nfds_t) (2 + g_clientCount), timeout);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            logmsg("poll failed: %s", strerror(errno));
            break;
        }

        /* UART обрабатывается раньше клиентов: пропавший порт отключает их всех разом. */
        if (g_uart >= 0) {
            short revents = fds[1].revents;
            int lost = 0;
            ssize_t got = 0;
            if (revents & POLLIN) {
                got = read(g_uart, buf, sizeof buf);
                if (got > 0)
                    broadcast(buf, (size_t) got);
                else if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                    lost = 1;
            }
            if (!lost && got <= 0 && (revents & (POLLHUP | POLLERR | POLLNVAL)))
                lost = 1;
            if (!lost && (revents & POLLOUT) && pendingFlush() != 0)
                lost = 1;
            if (lost) {
                logmsg("serial port lost");
                uartClose("disconnected (serial port lost)");
                nextOpen = nowMs() + UART_RETRY_MS;
                continue;
            }
        }

        if (fds[0].revents & POLLIN)
            acceptClient(&cfg);

        /* С конца к началу: закрытие подменяет текущий элемент последним, и обход вперёд
         * пропустил бы подставленного. */
        for (int i = g_clientCount - 1; i >= 0; --i) {
            if (g_uart < 0)
                break;
            /* Клиент, принятый в этом проходе, или переставленный закрытием при раздаче из
             * UART, в fds не соответствует своему месту: его очередь придёт со следующим
             * poll() (события уровневые, ничего не теряется). */
            if (fds[2 + i].fd != g_clients[i].fd)
                continue;
            short revents = fds[2 + i].revents;

            if ((revents & POLLOUT) && clientFlush(&g_clients[i]) != 0) {
                clientClose(i, "disconnected");
                continue;
            }
            if (revents & POLLIN) {
                int rc = clientRead(i);
                if (rc == -2) {
                    logmsg("serial port lost");
                    uartClose("disconnected (serial port lost)");
                    nextOpen = nowMs() + UART_RETRY_MS;
                    break;
                }
                if (rc != 0) {
                    clientClose(i, "disconnected");
                    continue;
                }
            } else if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
                clientClose(i, "disconnected");
            }
        }
    }

    logmsg("stopping");
    uartClose("disconnected (shutting down)");
    if (g_listen >= 0)
        close(g_listen);
    if (pidPath)
        unlink(pidPath);
    return 0;
}
