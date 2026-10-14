#!/bin/sh
# Установка или обновление одного экземпляра агента spotty-uartd. Запускается от root.
#
#   install.sh ПОРТ АВТОЗАПУСК
#
# ПОРТ — номер TCP-порта, он же имя экземпляра; АВТОЗАПУСК — 1 или 0. Больше скрипт ничего не
# принимает: всё остальное лежит рядом готовыми файлами, чтобы строки пользователя (путь к
# порту, список адресов) никогда не попадали в командную строку.
#
# Рядом должны лежать: spotty-uartd, spotty-uartd@.service, ПОРТ.conf.
#
# Для проверок есть переменные: SPOTTY_ROOT — корень вместо /, SPOTTY_PROC — каталог вместо /proc,
# SPOTTY_SYSTEMD=yes|no|auto, SPOTTY_SYSTEMCTL — подмена systemctl. В работе не используются.
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

[ $# -eq 2 ] || fail "usage: install.sh PORT AUTOSTART"
port=$1
autostart=$2

# Порт попадает в имена файлов и сервисов: только цифры.
case $port in ''|*[!0-9]*) fail "bad port '$port'";; esac
[ "$port" -ge 1 ] && [ "$port" -le 65535 ] || fail "bad port '$port'"
case $autostart in 0|1) ;; *) fail "bad autostart flag '$autostart'";; esac

stage=$(cd "$(dirname "$0")" && pwd)
root=${SPOTTY_ROOT:-}
proc=${SPOTTY_PROC:-/proc}
bin_dir=$root/usr/local/bin
conf_dir=$root/etc/spotty-uartd
unit_dir=$root/etc/systemd/system
systemctl=${SPOTTY_SYSTEMCTL:-systemctl}
unit="spotty-uartd@$port.service"

for file in spotty-uartd spotty-uartd@.service "$port.conf"; do
    [ -f "$stage/$file" ] || fail "$file is missing next to the script"
done

case ${SPOTTY_SYSTEMD:-auto} in
    yes) use_systemd=1 ;;
    no) use_systemd=0 ;;
    *) if [ -d /run/systemd/system ]; then use_systemd=1; else use_systemd=0; fi ;;
esac

mkdir -p "$bin_dir" "$conf_dir"

echo "STEP: installing the agent"
# Через временное имя и mv: запущенный бинарник перезаписать нельзя («Text file busy»), а
# переименование заменяет его атомарно, и работающий экземпляр доживает на старом.
install -m 755 "$stage/spotty-uartd" "$bin_dir/.spotty-uartd.new"
mv -f "$bin_dir/.spotty-uartd.new" "$bin_dir/spotty-uartd"

echo "STEP: writing the configuration"
install -m 644 "$stage/$port.conf" "$conf_dir/$port.conf"

if [ "$use_systemd" -eq 1 ]; then
    echo "STEP: setting up the service"
    mkdir -p "$unit_dir"
    install -m 644 "$stage/spotty-uartd@.service" "$unit_dir/spotty-uartd@.service"
    "$systemctl" daemon-reload
    if [ "$autostart" -eq 1 ]; then
        "$systemctl" enable "$unit" >/dev/null 2>&1 || fail "cannot enable $unit"
    else
        # Перенастройка с автозапуском на «без»: включённый ранее сервис сам не выключится.
        "$systemctl" disable "$unit" >/dev/null 2>&1 || true
    fi
    echo "STEP: starting the agent"
    "$systemctl" restart "$unit" || fail "cannot start $unit"
    sleep 1
    if ! "$systemctl" is-active --quiet "$unit"; then
        echo "--- journal ---" >&2
        journalctl -u "$unit" -n 15 --no-pager 2>/dev/null >&2 || true
        fail "$unit did not stay running"
    fi
else
    # Без systemd: процесс отвязывается от сеанса ssh и живёт до перезагрузки. Автозапуска у
    # такой установки нет — установщик об этом предупреждает ещё до начала.
    echo "STEP: starting the agent"
    if [ -d "$root/run" ]; then run_dir=$root/run; else run_dir=$root/var/run; fi
    mkdir -p "$run_dir"
    pidfile=$run_dir/spotty-uartd-$port.pid
    if [ -f "$pidfile" ]; then
        old=$(cat "$pidfile" 2>/dev/null || true)
        if [ -n "$old" ] && is_agent "$old"; then
            kill "$old" 2>/dev/null || true
            sleep 1
        fi
    fi
    log=$root/var/log/spotty-uartd-$port.log
    mkdir -p "$(dirname "$log")"
    if command -v setsid >/dev/null 2>&1; then
        setsid nohup "$bin_dir/spotty-uartd" --config "$conf_dir/$port.conf" --pidfile "$pidfile" \
            >"$log" 2>&1 </dev/null &
    else
        nohup "$bin_dir/spotty-uartd" --config "$conf_dir/$port.conf" --pidfile "$pidfile" \
            >"$log" 2>&1 </dev/null &
    fi
    sleep 1
    new=$(cat "$pidfile" 2>/dev/null || true)
    if [ -z "$new" ] || ! kill -0 "$new" 2>/dev/null; then
        tail -n 15 "$log" >&2 2>/dev/null || true
        fail "the agent did not stay running"
    fi
fi

echo "OK"
