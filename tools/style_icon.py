#!/usr/bin/env python3
"""Делает из плоского мастера значка плашки в стиле Windows 11 и Linux (GNOME/KDE).

    tools/style_icon.py resources/icons/spotty-master.png \
        resources/icons/spotty-master-win.png resources/icons/spotty-master-linux.png
    tools/style_icon.py resources/icons/spotty-master-lamp.png \
        resources/icons/spotty-master-lamp-win.png resources/icons/spotty-master-lamp-linux.png

Рисунок один и тот же на всех системах; различается только оболочка, как у значков самих систем:

- **Windows (Fluent, Windows 11).** Плоская плашка со скруглением поменьше, чем у macOS, почти
  на весь холст (поля 3 %: на панели задач значок мелкий, и каждый пиксель на счету), без
  тени — панель задач её не рисует — и с тонкой светлой обводкой внутри, чтобы тёмная плашка не
  сливалась с тёмной панелью.
- **Linux (GNOME HIG / Breeze).** Более круглая плашка с полями 7 % под мягкую тень: оболочки
  рабочих столов сами теней не дорисовывают, а значок без неё выглядит вырезанным. Верхняя
  кромка чуть светлее, внешняя — тонкая тёмная линия.

macOS в этом нет: значок бандла получает системную оболочку сам, а значок запущенной программы
берёт хромовый мастер tools/frame_icon.py.

Результат детерминирован. Нужны numpy и Pillow.
"""
import sys

import numpy as np
from PIL import Image, ImageFilter

SIZE = 1024
SUPER = 3  # суперсэмплинг: края плашки считаются на утроенной сетке


def shape_mask(side, half, kind, radius=0.0, n=0.0):
    """Маска плашки полуразмера `half` px по центру холста side×side (в суперсэмплах).

    kind="rounded" — прямоугольник со скруглением radius (доля стороны), "squircle" — суперэллипс
    степени n.
    """
    ys, xs = np.mgrid[0:side, 0:side].astype(np.float32)
    dx, dy = np.abs(xs + 0.5 - side / 2), np.abs(ys + 0.5 - side / 2)
    if kind == "squircle":
        return ((dx / half) ** n + (dy / half) ** n <= 1.0).astype(np.float32)
    r = radius * 2 * half
    # Расстояние до скруглённого прямоугольника со знаком: внутри ≤ 0.
    qx, qy = dx - (half - r), dy - (half - r)
    outside = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
    inside = np.minimum(np.maximum(qx, qy), 0)
    return (outside + inside - r <= 0).astype(np.float32)


def to_l(mask, side_px):
    img = Image.fromarray((mask * 255).astype(np.uint8), "L")
    return img.resize((side_px, side_px), Image.LANCZOS)


def layer(rgb, alpha_l, opacity):
    """RGBA-слой одного цвета с заданной маской и непрозрачностью."""
    a = alpha_l.point(lambda v: int(v * opacity))
    out = Image.new("RGBA", alpha_l.size, rgb + (0,))
    out.putalpha(a)
    return out


def styled(art, plate_frac, kind, radius, n, shadow, rim):
    """Собирает значок: art — плоский мастер RGBA, plate_frac — доля холста под плашку."""
    plate = round(SIZE * plate_frac)
    offset = (SIZE - plate) // 2
    sup = plate * SUPER

    def full(inset=0.0, shift=(0.0, 0.0)):
        """Маска плашки на холсте SIZE; inset сужает её, shift сдвигает (в пикселях холста)."""
        m = shape_mask(sup, plate / 2 * SUPER - inset * SUPER, kind, radius, n)
        if shift != (0.0, 0.0):
            m = np.roll(m, (round(shift[1] * SUPER), round(shift[0] * SUPER)), axis=(0, 1))
        canvas = Image.new("L", (SIZE, SIZE), 0)
        canvas.paste(to_l(m, plate), (offset, offset))
        return canvas

    body = full()
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))

    if shadow:
        blur, dy, opacity = shadow
        soft = Image.new("L", (SIZE, SIZE), 0)
        soft.paste(to_l(shape_mask(sup, plate / 2 * SUPER, kind, radius, n), plate), (offset, offset + dy))
        out.alpha_composite(layer((0, 0, 0), soft.filter(ImageFilter.GaussianBlur(blur)), opacity))

    picture = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    picture.paste(art.resize((plate, plate), Image.LANCZOS), (offset, offset))
    picture.putalpha(body)
    out.alpha_composite(picture)

    for width, rgb, opacity, shift in rim:
        # Кольцо шириной width px: плашка минус её же копия, суженная и сдвинутая.
        inner = np.asarray(full(inset=width, shift=shift), np.float32)
        ring = np.clip(np.asarray(body, np.float32) - inner, 0, 255).astype(np.uint8)
        out.alpha_composite(layer(rgb, Image.fromarray(ring, "L"), opacity))
    return out


def main(master_path, win_path, linux_path):
    art = Image.open(master_path).convert("RGBA")

    # Windows: плашка 94 %, скругление 17 %, без тени, светлая обводка 4 px изнутри.
    styled(art, 0.94, "rounded", 0.17, 0.0, None, [(4, (255, 255, 255), 0.22, (0, 0))]).save(
        win_path, optimize=True)

    # Linux: плашка 86 %, скругление 22 %, тень (размытие 14 px, вниз на 14 px, 38 %), тёмная
    # внешняя линия и светлая верхняя кромка.
    styled(art, 0.86, "rounded", 0.22, 0.0, (14, 14, 0.38),
           [(3, (0, 0, 0), 0.40, (0, 0)), (4, (255, 255, 255), 0.18, (0, 5))]).save(
        linux_path, optimize=True)
    print(f"wrote {win_path}\nwrote {linux_path}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
