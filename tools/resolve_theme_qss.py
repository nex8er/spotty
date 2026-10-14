#!/usr/bin/env python3
"""Собрать готовый QSS темы Spotty для офскрин-снимков окон.

Подставляет в resources/themes/spotty.qss цвета и размеры (как ThemeManager::resolveStylesheet(),
размеры — для macOS) и пишет рядом `<файл>.pal` с цветами палитры. Результат читает хук
`SPOTTY_TEST_QSS` в tests/test_interface_settings_panel.cpp:

    python3 tools/resolve_theme_qss.py dark /tmp/dark.qss
    SPOTTY_TEST_QSS=/tmp/dark.qss SPOTTY_TEST_SNAPSHOT_DIR=/tmp/snap QT_QPA_PLATFORM=offscreen \\
        build/tests/spotty-ui-tests --gtest_filter='InterfaceSettingsPanel.TunnelDialog*'

Это приближение, а не сам ThemeManager: таблица плейсхолдеров продублирована здесь, и новый
`@имя` в QSS нужно добавить сюда — скрипт предупредит о нём на stderr.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SIZES = dict(radius=5, cardRadius=8, controlHeight=18, buttonMinWidth=48, padV=4, padH=12, gap=5,
             scrollBar=11, spinButton=13, spinButtonWidth=20, spinRightPadding=4)


def colors(theme: str) -> dict:
    source = (REPO / "src/ui/theme/ThemeManager.cpp").read_text(encoding="utf-8")
    start = source.index("ThemeColors %s()" % ("darkColors" if theme == "dark" else "lightColors"))
    body = source[start:source.index("return c;", start)]
    return {name: "#" + r + g + b
            for name, r, g, b in re.findall(r"c\.(\w+) = QColor\(0x(\w+), 0x(\w+), 0x(\w+)\)", body)}


def rgba(hex_color: str, alpha: int) -> str:
    r, g, b = (int(hex_color[i:i + 2], 16) for i in (1, 3, 5))
    return f"rgba({r}, {g}, {b}, {alpha})"


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in ("dark", "light"):
        print(__doc__, file=sys.stderr)
        return 2
    c = colors(sys.argv[1])
    subs = {"@" + name: c[key] for name, key in {
        "window": "window", "panel": "panel", "base": "base", "borderControl": "controlBorder",
        "border": "border", "text": "text", "textMuted": "textMuted",
        "accentSolidPressed": "accentSolidPressed", "accentSolidHover": "accentSolidHover",
        "accentSolid": "accentSolid", "accentText": "accentText", "accent": "accent",
        "selection": "selection", "hover": "hover", "pressed": "pressed", "errorText": "errorText",
        "okText": "okText"}.items()}
    subs.update({"@scrollHandleHover": rgba(c["textMuted"], 210), "@scrollHandle": rgba(c["textMuted"], 105),
                 "@fontTitle": "14pt", "@fontSmall": "12pt"})
    subs.update({"@" + name: f"{value}px" for name, value in SIZES.items()})

    sheet = (REPO / "resources/themes/spotty.qss").read_text(encoding="utf-8")
    for placeholder in sorted(subs, key=len, reverse=True):  # длинные первыми: @text до @textMuted затёр бы его
        sheet = sheet.replace(placeholder, subs[placeholder])
    sheet = sheet.replace("url(:/icons/", f"url({REPO}/resources/icons/")
    leftover = set(re.findall(r"@[A-Za-z]\w*", re.sub(r"/\*.*?\*/", "", sheet, flags=re.S)))
    if leftover:
        print("Остались плейсхолдеры:", ", ".join(sorted(leftover)), file=sys.stderr)

    out = Path(sys.argv[2])
    out.write_text(sheet, encoding="utf-8")
    Path(str(out) + ".pal").write_text(
        " ".join(c[k] for k in ("window", "text", "base", "panel", "accent", "textMuted")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
