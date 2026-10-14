#!/bin/sh
# Удаление одного экземпляра агента spotty-uartd. Запускается от root.
#
#   uninstall.sh ПОРТ
#
# Последний экземпляр уносит с собой и бинарник, и шаблон сервиса: пустой каталог и
# одинокий бинарник на чужой машине — мусор, который никто потом не найдёт.
#
# Переменные для проверок — как у install.sh.
set -eu

fail() { echo "ERROR: $*" >&2; exit 1; }

# Принадлежит ли pid нашему агенту. Номер из pid-файла мог за это время достаться другому
# процессу, и убивать вслепую нельзя. /proc есть везде, где есть Linux; ps — для проверки на
# других системах (на busybox у него нет ни -p, ни -o).
is_agent() {
    if [ -r "$proc/$1/cmdline" ]; then
        tr '\0' ' ' <"$proc/$1/cmdline" | grep -q spotty-uartd
    else
        ps -p "$1" -o args= 2>/dev/null | grep -q spotty-uartd
    fi
}

[ $# -eq 1 ] || fail "usage: uninstall.sh PORT"
port=$1
case $port in ''|*[!0-9]*) fail "bad port '$port'";; esac
[ "$port" -ge 1 ] && [ "$port" -le 65535 ] || fail "bad port '$port'"

root=${SPOTTY_ROOT:-}
proc=${SPOTTY_PROC:-/proc}
bin_dir=$root/usr/local/bin
conf_dir=$root/etc/spotty-uartd
unit_dir=$root/etc/systemd/system
systemctl=${SPOTTY_SYSTEMCTL:-systemctl}
unit="spotty-uartd@$port.service"

case ${SPOTTY_SYSTEMD:-auto} in
    yes) use_systemd=1 ;;
    no) use_systemd=0 ;;
    *) if [ -d /run/systemd/system ]; then use_systemd=1; else use_systemd=0; fi ;;
esac

echo "STEP: stopping the agent"
if [ "$use_systemd" -eq 1 ]; then
    "$systemctl" disable --now "$unit" >/dev/null 2>&1 || true
    "$systemctl" reset-failed "$unit" >/dev/null 2>&1 || true
fi
if [ -d "$root/run" ]; then run_dir=$root/run; else run_dir=$root/var/run; fi
pidfile=$run_dir/spotty-uartd-$port.pid
if [ -f "$pidfile" ]; then
    old=$(cat "$pidfile" 2>/dev/null || true)
    if [ -n "$old" ] && is_agent "$old"; then
        kill "$old" 2>/dev/null || true
    fi
    rm -f "$pidfile"
fi

echo "STEP: removing the files"
rm -f "$conf_dir/$port.conf" "$root/var/log/spotty-uartd-$port.log"

remaining=0
for conf in "$conf_dir"/*.conf; do
    [ -f "$conf" ] && remaining=$((remaining + 1))
done
if [ "$remaining" -eq 0 ]; then
    rm -f "$bin_dir/spotty-uartd" "$unit_dir/spotty-uartd@.service"
    rmdir "$conf_dir" 2>/dev/null || true
    if [ "$use_systemd" -eq 1 ]; then "$systemctl" daemon-reload; fi
fi

echo "OK"
