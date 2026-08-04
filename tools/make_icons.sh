#!/usr/bin/env bash
# Пересобирает платформенные значки из resources/icons/spotty-master.png.
#
# Мастер-файл — не то, что порождается заново из кода, как MdiCodepoints.h: это
# отдельный дизайн, обработанный один раз (обрезан по границе скруглённого квадрата,
# внешний фон срезан альфа-маской) и зафиксированный в репозитории как исходник. Скрипт
# нужен только когда этот исходник меняют — например, дизайн иконки правится заново.
#
#     tools/make_icons.sh
#
# Требует ImageMagick (`magick`) и, для .icns, macOS с `iconutil` — на других системах
# эта часть пропускается, а не завершается ошибкой: .icns в репозитории уже лежит
# готовым, и его отсутствие в пересборке на Linux/Windows не критично.
set -euo pipefail

cd "$(dirname "$0")/.."
ICONS_DIR="resources/icons"
MASTER="$ICONS_DIR/spotty-master.png"

if [[ ! -f "$MASTER" ]]; then
    echo "error: $MASTER not found" >&2
    exit 1
fi

if ! command -v magick >/dev/null 2>&1; then
    echo "error: ImageMagick (magick) not found" >&2
    exit 1
fi

# --- macOS: .iconset -> .icns -----------------------------------------------------------
if command -v iconutil >/dev/null 2>&1; then
    iconset=$(mktemp -d)/spotty.iconset
    mkdir -p "$iconset"

    for size in 16 32 128 256 512; do
        magick "$MASTER" -resize "${size}x${size}" "$iconset/icon_${size}x${size}.png"
        double=$((size * 2))
        magick "$MASTER" -resize "${double}x${double}" "$iconset/icon_${size}x${size}@2x.png"
    done

    iconutil -c icns "$iconset" -o "$ICONS_DIR/spotty.icns"
    rm -rf "$(dirname "$iconset")"
    echo "wrote $ICONS_DIR/spotty.icns"
else
    echo "iconutil not found - skipping .icns (macOS only)"
fi

# --- Windows: многоразмерный .ico --------------------------------------------------------
magick "$MASTER" -define icon:auto-resize=256,128,64,48,32,16 "$ICONS_DIR/spotty.ico"
echo "wrote $ICONS_DIR/spotty.ico"

# --- Linux: одна PNG для AppImage ---------------------------------------------------------
magick "$MASTER" -resize 256x256 "$ICONS_DIR/spotty-256.png"
echo "wrote $ICONS_DIR/spotty-256.png"
