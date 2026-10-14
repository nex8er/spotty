#!/bin/sh
# Сведения об удалённой машине для установщика Spotty: строки «ключ=значение», по одной на
# факт; ключи, у которых значений несколько (instance, listen_hex), повторяются.
#
# Запускается от обычного пользователя, ничего не меняет. Только POSIX sh: на плате может
# быть busybox, а не bash.
#
# Для проверок есть две переменные: SPOTTY_ROOT — корень вместо / для файлов машины (os-release,
# каталог systemd, конфиги агента), SPOTTY_PROC — каталог вместо /proc. Так разбор вывода Linux
# проверяется на образцах и на других системах. В работе не используются.
root=${SPOTTY_ROOT:-}
proc=${SPOTTY_PROC:-/proc}

echo "uname_s=$(uname -s 2>/dev/null)"
echo "uname_m=$(uname -m 2>/dev/null)"
echo "dpkg_arch=$(dpkg --print-architecture 2>/dev/null)"
echo "long_bit=$(getconf LONG_BIT 2>/dev/null)"
echo "uid=$(id -u 2>/dev/null)"
# Имя машины: из него Spotty составляет подпись подключения (имя:порт вместо адреса:порт).
echo "hostname=$(hostname 2>/dev/null || uname -n 2>/dev/null)"

# Первые 40 байт /bin/sh: по ним определяется разрядность и «жёсткость» float у 32-битного
# ARM, когда dpkg нет (Alpine, Arch ARM, самодельные образы).
echo "elf=$(od -An -v -tx1 -N40 /bin/sh 2>/dev/null | tr -d ' \n')"

if [ -r "$root/etc/os-release" ]; then
    echo "os_name=$(sed -n 's/^PRETTY_NAME=//p' "$root/etc/os-release" | head -n 1 | tr -d '"')"
fi

# Способ, которым сам systemd проверяет, что система им загружена (sd_booted()): наличие
# каталога, а не бинарника systemctl — в контейнере и в chroot бинарник бывает без демона.
if [ -d "$root/run/systemd/system" ]; then echo "systemd=yes"; else echo "systemd=no"; fi

if command -v sudo >/dev/null 2>&1; then echo "sudo=yes"; else echo "sudo=no"; fi

# Установленный ранее агент и его экземпляры: порт и порт UART.
if [ -x "$root/usr/local/bin/spotty-uartd" ]; then
    echo "installed=$("$root/usr/local/bin/spotty-uartd" --version 2>/dev/null)"
fi
for conf in "$root"/etc/spotty-uartd/*.conf; do
    [ -f "$conf" ] || continue
    port=$(basename "$conf" .conf)
    device=$(sed -n 's/^[[:space:]]*device[[:space:]]*=[[:space:]]*//p' "$conf" | head -n 1)
    echo "instance=$port|$device"
done

# Занятые порты TCP: из /proc, а не через ss или netstat — их на плате может не быть, а /proc
# есть всегда. Состояние 0A — LISTEN, порт в шестнадцатеричном виде.
for table in "$proc/net/tcp" "$proc/net/tcp6"; do
    [ -r "$table" ] || continue
    awk 'NR > 1 && $4 == "0A" { n = split($2, a, ":"); print "listen_hex=" a[n] }' "$table"
done
exit 0
