#!/usr/bin/env bash
# Кросс-сборка агента spotty-uartd под все поддерживаемые архитектуры Linux.
#
#   agent/build-agents.sh            собрать всё в resources/agent/<арх>/spotty-uartd
#   agent/build-agents.sh aarch64    собрать одну архитектуру
#
# Нужен только `zig` (brew install zig / apt install zig): его `zig cc` несёт musl и
# сразу кросс-компилирует под все цели, отдельные тулчейны не нужны. Бинарники статические
# и лежат в репозитории готовыми, поэтому обычной сборке Spotty zig не требуется — он
# нужен тому, кто меняет сам агент.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_file="$root/agent/spotty-uartd.c"
out_root="$root/resources/agent"
version="$(head -n 1 "$root/agent/VERSION")"

if ! command -v zig >/dev/null 2>&1; then
    echo "zig not found: install it first (brew install zig)" >&2
    exit 1
fi

# Имя каталога | цель zig | флаги процессора | что `file` пишет об архитектуре (строчными)
#
# Имя каталога совпадает с тем, что определяет установщик на удалённом хосте. Три ARM-
# варианта различаются ядром, на котором бинарник способен стартовать: armv7 (armhf) не
# запустится на Raspberry Pi Zero/1 (armv6), а armel (программный float) собран под ARMv6 и
# идёт на любом ядре от ARMv6 и новее.
#
# Настоящий ARMv5 (старые NAS на Kirkwood) не поддержан: ему нужны атомарные операции через
# libcalls, которых в libc самого zig нет — сборка под arm926ej_s падает на линковке с
# `undefined symbol: __sync_*`. Для armel-userland на современном ARM это не помеха.
targets=(
    "x86_64|x86_64-linux-musl||x86-64"
    "i386|x86-linux-musl|-mcpu=i686|80386"
    "aarch64|aarch64-linux-musl||aarch64"
    "armv7|arm-linux-musleabihf|-mcpu=cortex_a7|arm, eabi5"
    "armv6|arm-linux-musleabihf|-mcpu=arm1176jzf_s|arm, eabi5"
    "armel|arm-linux-musleabi|-mcpu=arm1176jzf_s|arm, eabi5"
    "riscv64|riscv64-linux-musl||risc-v"
)

only="${1:-}"
built=0
for entry in "${targets[@]}"; do
    IFS='|' read -r name triple cpu machine <<<"$entry"
    if [[ -n "$only" && "$only" != "$name" ]]; then
        continue
    fi

    out="$out_root/$name"
    mkdir -p "$out"
    # shellcheck disable=SC2086 # $cpu пуст или один флаг
    zig cc -target "$triple" $cpu -Os -s -static \
        -std=gnu11 -Wall -Wextra \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -DSPOTTY_UARTD_VERSION="\"$version\"" \
        -o "$out/spotty-uartd" "$source_file"

    # Архитектура проверяется по самому файлу: опечатка в таблице выше иначе дала бы
    # бинарник не под ту плату, и узнали бы об этом на удалённом хосте по «Exec format error».
    if command -v file >/dev/null 2>&1; then
        description="$(file -b "$out/spotty-uartd" | tr '[:upper:]' '[:lower:]')"
        if [[ "$description" != *"$machine"* || "$description" != *"statically linked"* ]]; then
            echo "$name: unexpected result: $description" >&2
            exit 1
        fi
    fi
    printf '%-8s %7s bytes  %s\n' "$name" "$(wc -c <"$out/spotty-uartd" | tr -d ' ')" "$triple $cpu"
    built=$((built + 1))
done

if [[ "$built" -eq 0 ]]; then
    echo "unknown architecture: $only" >&2
    exit 1
fi

# Версия рядом с бинарниками: установщик сравнивает её с установленной на хосте.
printf '%s\n' "$version" >"$out_root/VERSION"
echo "agent $version, zig $(zig version)"
