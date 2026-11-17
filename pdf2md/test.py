#!/usr/bin/env python3
"""Integration test for pdf2md.

Usage:
    python test.py "D:\\path\\document.pdf"
"""

from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

from pdf2md import convert_pdf


DEFAULT_PDF = Path(r"D:\work\IPC\低功耗\ATBM6463\DOC\ATBM6463 AT command.pdf")


def main() -> int:
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PDF
    if not source.is_file():
        print(f"PDF 不存在: {source}", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="pdf2md-test-") as directory:
        output = Path(directory) / (source.stem + ".md")
        report = convert_pdf(source, output, workers=4, images="first", strict=True)
        markdown = output.read_text(encoding="utf-8")
        checks = {
            "output_nonempty": len(markdown) > 100,
            "page_markers": markdown.count("<!-- PDF 第 ") == report["pages"],
            "markdown_tables": bool(re.search(r"(?m)^\|(?:---\|)+$", markdown)),
            "no_code_fences": "```" not in markdown,
            "headings_complete": report["matched_headings"] == report["toc_entries"],
            "characters_complete": not report["char_mismatch_pages"],
        }
        for name, passed in checks.items():
            print(f"{'PASS' if passed else 'FAIL'}  {name}")
        print(report)
        return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())

