#!/usr/bin/env python3
"""Надевает на плоский мастер значка тонкую хромовую рамку по squircle.

    tools/frame_icon.py resources/icons/spotty-master.png      resources/icons/spotty-master-framed.png
    tools/frame_icon.py resources/icons/spotty-master-lamp.png resources/icons/spotty-master-lamp-framed.png

Зачем она нужна. Плоский мастер — полный квадрат без рамки: на macOS 26+ систему он устраивает,
потому что она сама обрезает значок бандла по своему squircle и сама рисует светлую фаску, ужимая
рисунок примерно до 83 % (замерено отрисовкой `NSWorkspace.icon(forFile:)`: тело 824 из 1024,
окно под рисунок 679). Остальным путям эту рамку никто не нарисует — значку окна и доку у
запущенной программы (PNG 256 ставит `QApplication::setWindowIcon`), Windows (.ico), AppImage — а
рисунок без неё выглядит чужим рядом со значками системы. Здесь рамка тонкая (14 px) и хромовая: тот же градиент и
фаски, что у скрытых слоёв `frame-rim` в `logo/*.vectorcraft`.

Для `.icns` рамочный мастер брать нельзя: система наложила бы свою фаску поверх этой, и вышла бы
двойная рамка в рамке. Поэтому `make_icons.sh` берёт `.icns` из плоского мастера, а `.ico` и
PNG 256 — из рамочного.

Результат детерминирован. Нужны numpy и Pillow.
"""
import sys

import numpy as np
from PIL import Image

SIZE = 1024
SUPER = 3                      # суперсэмплинг: края рамки считаются на утроенной сетке
OUTER_HALF, OUTER_N = 512, 4.5  # внешний squircle — на весь холст
INNER_HALF, INNER_N = 498, 5.0  # окно под рисунок: тонкая рамка шириной 14 px

# Хромовый градиент по диагонали из верхнего левого угла — те же семь остановок, что у слоя
# frame-rim в проектах logo/*.vectorcraft.
CHROME_STOPS = [
    (0.00, "#F4F6FA"), (0.17, "#8F959F"), (0.34, "#23252A"), (0.50, "#555963"),
    (0.67, "#C3C8D2"), (0.84, "#3B3E45"), (1.00, "#7A7F89"),
]


def hex_rgb(value):
    return np.array([int(value[i:i + 2], 16) for i in (1, 3, 5)], np.float32)


def main(master_path, out_path):
    side = SIZE * SUPER
    centre = side / 2
    ys, xs = np.mgrid[0:side, 0:side].astype(np.float32)
    dx, dy = (xs + 0.5 - centre) / SUPER, (ys + 0.5 - centre) / SUPER

    def squircle(half, n, shift=(0.0, 0.0)):
        x, y = np.abs(dx - shift[0]) / half, np.abs(dy - shift[1]) / half
        return (x ** n + y ** n <= 1.0).astype(np.float32)

    outer = squircle(OUTER_HALF, OUTER_N)
    inner = squircle(INNER_HALF, INNER_N)

    # Хром.
    t = np.clip(((dx + dy) / (2 * OUTER_HALF) + 1) / 2, 0, 1)
    canvas = np.zeros((side, side, 3), np.float32)
    for (t0, c0), (t1, c1) in zip(CHROME_STOPS, CHROME_STOPS[1:]):
        mask = (t >= t0) & (t <= t1)
        k = ((t - t0) / (t1 - t0))[mask][:, None]
        canvas[mask] = hex_rgb(c0) * (1 - k) + hex_rgb(c1) * k

    # Блик по верхней-левой кромке и тень по нижней-правой: внешняя форма минус её же копия,
    # сдвинутая в противоположную сторону.
    highlight = np.clip(outer - squircle(OUTER_HALF, OUTER_N, (2.0, 2.5)), 0, 1)
    shade = np.clip(outer - squircle(OUTER_HALF, OUTER_N, (-2.0, -2.5)), 0, 1)
    canvas = canvas * (1 - 0.65 * highlight[..., None]) + 255 * 0.65 * highlight[..., None]
    canvas = canvas * (1 - 0.45 * shade[..., None])

    # Тёмный жёлоб вокруг окна и тонкая фаска за ним.
    groove = np.clip(squircle(INNER_HALF + 3, INNER_N) - inner, 0, 1)
    canvas = canvas * (1 - 0.85 * groove[..., None])
    bevel = np.clip(squircle(INNER_HALF + 5, INNER_N) - squircle(INNER_HALF + 3, INNER_N), 0, 1)
    canvas = canvas * (1 - 0.25 * bevel[..., None])

    # Рисунок в окне.
    window_size = INNER_HALF * 2 * SUPER
    art = np.asarray(
        Image.open(master_path).convert("RGB").resize((window_size, window_size), Image.LANCZOS),
        np.float32,
    )
    offset = (side - window_size) // 2
    window = np.zeros_like(canvas)
    window[offset:offset + window_size, offset:offset + window_size] = art
    canvas = canvas * (1 - inner[..., None]) + window * inner[..., None]

    def shrink(img):
        return img.resize((SIZE, SIZE), Image.LANCZOS)

    rgb = shrink(Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), "RGB")).convert("RGBA")
    alpha = shrink(Image.fromarray((outer * 255).astype(np.uint8), "L"))
    rgb.putalpha(alpha)
    rgb.save(out_path, optimize=True)
    print(f"wrote {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
