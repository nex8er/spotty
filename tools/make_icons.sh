#!/usr/bin/env bash
# Пересобирает платформенные значки из мастер-файлов resources/icons/.
#
# Мастер-файл — не то, что порождается заново из кода, как MdiCodepoints.h: это
# отдельный дизайн, обработанный один раз и зафиксированный в репозитории как исходник.
# Скрипт нужен только когда этот исходник меняют — например, дизайн иконки правится заново.
#
#     tools/make_icons.sh
#
# Мастеров два (обычный и с лампой), и у каждого четыре формы — по одной на систему:
#
#   spotty-master.png        (плоский, macOS)    -> spotty.icns
#   spotty-master-framed.png (хромовая рамка)    -> spotty-256.png        (значок запущенной программы на macOS)
#   spotty-master-win.png    (Windows 11)        -> spotty.ico, spotty-win-256.png
#   spotty-master-linux.png  (GNOME/KDE, тень)   -> spotty-linux-256.png  (AppImage и значок окна)
#   spotty-master-lamp*.png                      -> то же с префиксом spotty-lamp
#
# Формы Windows и Linux делает tools/style_icon.py из плоского мастера, рамочную — tools/frame_icon.py.
#
# Основной набор — значок файла и бандла (.icns, .ico, PNG 256 для AppImage) и значок окна без
# подключения. Вариант с лампочкой (оранжевая лампа в кольце первой ветки) — значок окна, пока
# открыт хоть один интерфейс: приложение переключает их само (MainWindow::updateApplicationIcon()),
# из обоих наборов вшиты только PNG 256. `.icns` и `.ico` набора с лампой сборка пока не берёт.
# Мастер с лампочкой необязателен — нет файла, этот набор пропускается, а не завершается
# ошибкой.
#
# Плоский мастер — полный квадрат на весь холст без рамки, тени и полей: с macOS 26 система сама
# обрезает значок бандла по своему squircle и накладывает свою фаску, и чужая рамка, нарисованная
# по другой кривой, оказывается срезана и ложится двойной. Поэтому `.icns` собирается из плоского.
# Док запущенной программы на macOS, Windows и Linux оболочку сами не рисуют, и каждой системе
# нужна своя — в стиле её значков. Мастера оболочек делаются при смене дизайна, скрипт сборки
# их не запускает: нужны numpy и Pillow.
#
# Качество уменьшения. Мастер 1024 px уменьшается до 16–512 px фильтром Lanczos; на размерах до
# SHARPEN_UP_TO добавляется лёгкая резкость — иначе линии толщиной в пиксель теряются, — а на
# больших она только прибавляла бы ореолов. Размер, равный мастеру, копируется без пересчёта.
#
# Требует ImageMagick (`magick`) и, для .icns, macOS с `iconutil` — на других системах
# эта часть пропускается, а не завершается ошибкой: .icns в репозитории уже лежит
# готовым, и его отсутствие в пересборке на Linux/Windows не критично.
set -euo pipefail

cd "$(dirname "$0")/.."
ICONS_DIR="resources/icons"
MASTER="$ICONS_DIR/spotty-master.png"
LAMP_MASTER="$ICONS_DIR/spotty-master-lamp.png"

# Размер, до которого включается лёгкая резкость: на 16–48 px линии толщиной в пиксель иначе
# теряются, на больших размерах резкость только прибавляет ореолов.
SHARPEN_UP_TO=48

if [[ ! -f "$MASTER" ]]; then
    echo "error: $MASTER not found" >&2
    exit 1
fi

if ! command -v magick >/dev/null 2>&1; then
    echo "error: ImageMagick (magick) not found" >&2
    exit 1
fi

# Уменьшает мастер до size×size в высоком качестве (см. «Качество уменьшения» выше).
#   $1 — мастер, $2 — размер стороны, $3 — выходной PNG.
resize_to() {
    local master="$1" size="$2" out="$3" master_size
    master_size=$(magick identify -format '%w' "$master")

    if [[ "$size" -eq "$master_size" ]]; then
        magick "$master" -strip -define png:compression-level=9 "$out"
    elif [[ "$size" -le "$SHARPEN_UP_TO" ]]; then
        magick "$master" -filter Lanczos -resize "${size}x${size}" -unsharp 0x0.45+0.45+0.02 \
            -strip -define png:compression-level=9 "$out"
    else
        magick "$master" -filter Lanczos -resize "${size}x${size}" \
            -strip -define png:compression-level=9 "$out"
    fi
}

# Собирает набор значков из мастеров одного рисунка.
#   $1 — имя набора: оно становится префиксом выходных файлов (spotty -> spotty.icns, ...),
#   $2 — плоский мастер (.icns), $3 — с хромовой рамкой, $4 — Windows, $5 — Linux.
build_icons() {
    local name="$1" master="$2" framed="$3" win="$4" linux="$5" size double tmp
    tmp=$(mktemp -d)

    # --- macOS: .iconset -> .icns -------------------------------------------------------
    if command -v iconutil >/dev/null 2>&1; then
        local iconset="$tmp/$name.iconset"
        mkdir -p "$iconset"

        for size in 16 32 128 256 512; do
            resize_to "$master" "$size" "$iconset/icon_${size}x${size}.png"
            double=$((size * 2))
            resize_to "$master" "$double" "$iconset/icon_${size}x${size}@2x.png"
        done

        iconutil -c icns "$iconset" -o "$ICONS_DIR/$name.icns"
        echo "wrote $ICONS_DIR/$name.icns"
    else
        echo "iconutil not found - skipping $name.icns (macOS only)"
    fi

    # --- Windows: многоразмерный .ico из кадров, уменьшенных отдельно -------------------
    local frames=()
    for size in 256 128 64 48 32 16; do
        resize_to "$win" "$size" "$tmp/ico_$size.png"
        frames+=("$tmp/ico_$size.png")
    done
    magick "${frames[@]}" "$ICONS_DIR/$name.ico"
    echo "wrote $ICONS_DIR/$name.ico"

    # --- PNG 256: окно запущенной программы (по одной на систему) и AppImage -------------
    resize_to "$framed" 256 "$ICONS_DIR/$name-256.png"
    resize_to "$win" 256 "$ICONS_DIR/$name-win-256.png"
    resize_to "$linux" 256 "$ICONS_DIR/$name-linux-256.png"
    echo "wrote $ICONS_DIR/$name-256.png, $name-win-256.png, $name-linux-256.png"

    rm -rf "$tmp"
}

# Мастера набора по префиксу: spotty -> spotty-master{,-framed,-win,-linux}.png,
# spotty-lamp -> spotty-master-lamp{,-framed,-win,-linux}.png.
build_set() {
    local name="$1" base="$ICONS_DIR/spotty-master${2}" part
    for part in "" -framed -win -linux; do
        if [[ ! -f "$base$part.png" ]]; then
            echo "$base$part.png not found - skipping $name"
            return 1
        fi
    done
    build_icons "$name" "$base.png" "$base-framed.png" "$base-win.png" "$base-linux.png"
}

build_set spotty "" || exit 1
build_set spotty-lamp -lamp || true
