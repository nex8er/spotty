#!/usr/bin/env python3
"""Проверяет покрытие по строкам против заданного порога.

    python3 tools/check_coverage.py <summary.json> <минимум-в-процентах>

На вход подаётся вывод `llvm-cov export -summary-only`. Скрипт печатает сводку по
функциям, строкам и ветвлениям и возвращает ненулевой код, если покрытие по строкам ниже
порога.

Отдельный скрипт, а не разбор текстового отчёта в shell: у `llvm-cov report` формат
таблицы менялся между версиями LLVM, и awk по колонкам ломался бы молча, показывая
неверную цифру вместо ошибки.
"""

import json
import sys
from pathlib import Path


def percent(covered: int, total: int) -> float:
    """Доля в процентах; пустое множество считается полностью покрытым."""
    return 100.0 if total == 0 else covered / total * 100.0


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2

    summary_path = Path(sys.argv[1])
    minimum = float(sys.argv[2])

    try:
        report = json.loads(summary_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"error: cannot read {summary_path}: {error}", file=sys.stderr)
        return 2

    try:
        totals = report["data"][0]["totals"]
    except (KeyError, IndexError):
        print(f"error: unexpected llvm-cov export format in {summary_path}", file=sys.stderr)
        return 2

    lines = totals["lines"]
    functions = totals["functions"]
    # Ветвления llvm-cov сообщает не всегда — зависит от версии и от флагов сборки.
    branches = totals.get("branches")

    line_percent = percent(lines["covered"], lines["count"])

    print()
    print("Покрытие spotty-core")
    print(f"  функции : {percent(functions['covered'], functions['count']):6.2f}%"
          f"  ({functions['covered']}/{functions['count']})")
    print(f"  строки  : {line_percent:6.2f}%  ({lines['covered']}/{lines['count']})")
    if branches and branches.get("count"):
        print(f"  ветвления: {percent(branches['covered'], branches['count']):6.2f}%"
              f"  ({branches['covered']}/{branches['count']})")
    print()

    if line_percent < minimum:
        print(f"ОШИБКА: покрытие по строкам {line_percent:.2f}% ниже порога {minimum:.2f}%",
              file=sys.stderr)
        return 1

    print(f"Порог {minimum:.2f}% пройден.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
