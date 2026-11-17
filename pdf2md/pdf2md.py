#!/usr/bin/env python3
"""Convert text-based PDFs to Markdown while preserving tables and TOC headings.

Usage:
    python pdf2md.py input.pdf [output.md]
    python pdf2md.py --dir input_dir [--out output_dir]
"""

from __future__ import annotations

import argparse
import concurrent.futures
import difflib
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import pymupdf


KNOWN_HEADERS = {
    ("Rev.", "Date", "Author", "Change Description"),
    ("指令", "描述", "指令示例"),
}


def normalize_heading(text: str) -> str:
    return "".join(char for char in text if not char.isspace())


def normalize_content(text: str) -> str:
    """Remove layout-only whitespace while retaining every visible character."""
    return "".join(char for char in text if not char.isspace())


def compare_chars(text: str) -> Counter:
    return Counter(normalize_content(text))


def point_in_rect(x: float, y: float, rect, tolerance: float = 1.0) -> bool:
    return (
        rect[0] - tolerance <= x <= rect[2] + tolerance
        and rect[1] - tolerance <= y <= rect[3] + tolerance
    )


def escape_cell(value: str | None) -> str:
    if value is None:
        return ""
    value = value.replace("\r\n", "\n").replace("\r", "\n").strip("\n")
    value = value.replace("|", "\\|")
    return value.replace("\n", "<br>")


def escape_inline_html(value: str) -> str:
    return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def plain_cell(value: str) -> str:
    return value.replace("<br>", "\n").replace("\\|", "|")


def locate_heading(items, title: str, used: set[int]):
    """Find a bookmark heading in one text line or two adjacent PDF text lines."""
    target = normalize_heading(title)
    candidates = []
    for index, item in enumerate(items):
        if item["type"] != "text" or index in used:
            continue
        value = normalize_heading(item["text"])
        if not value:
            continue
        score = difflib.SequenceMatcher(None, target, value, autojunk=False).ratio()
        candidates.append((score, index, (index,)))
        if index > 0 and items[index - 1]["type"] == "text" and index - 1 not in used:
            previous = normalize_heading(items[index - 1]["text"])
            combined = previous + value
            pair_score = difflib.SequenceMatcher(None, target, combined, autojunk=False).ratio()
            candidates.append((pair_score, index, (index - 1, index)))
    if not candidates:
        return None
    score, index, indices = max(candidates)
    return (index, indices) if score >= 0.86 else None


def _analyze_page(job):
    pdf_path, page_number, table_strategy = job
    document = pymupdf.open(pdf_path)
    page = document[page_number]
    page_dict = page.get_text("dict", sort=True)

    line_records = []
    for block_number, block in enumerate(page_dict["blocks"]):
        if block.get("type") != 0:
            continue
        for line_number, line in enumerate(block.get("lines", [])):
            text = "".join(span.get("text", "") for span in line.get("spans", []))
            if text:
                line_records.append(
                    {
                        "bbox": tuple(line["bbox"]),
                        "text": text,
                        "block": block_number,
                        "line": line_number,
                    }
                )

    def text_in_cell(cell) -> str:
        selected = []
        for record in line_records:
            bbox = record["bbox"]
            center_x = (bbox[0] + bbox[2]) / 2
            center_y = (bbox[1] + bbox[3]) / 2
            if point_in_rect(center_x, center_y, cell, tolerance=0):
                selected.append((bbox[1], bbox[0], record["text"]))
        selected.sort()
        return "\n".join(text for _, _, text in selected)

    found = page.find_tables(strategy=table_strategy)
    tables = []
    for table in found.tables:
        rows = []
        seen_cells = set()
        for row in table.rows:
            cells = []
            for cell in row.cells:
                cell_key = None if cell is None else tuple(round(value, 3) for value in cell)
                if cell_key is None or cell_key in seen_cells:
                    value = ""
                else:
                    seen_cells.add(cell_key)
                    value = text_in_cell(cell)
                cells.append(escape_cell(value))
            rows.append(cells)
        tables.append({"bbox": tuple(table.bbox), "rows": rows})

    text_items = []
    for record in line_records:
        bbox = record["bbox"]
        center_x = (bbox[0] + bbox[2]) / 2
        center_y = (bbox[1] + bbox[3]) / 2
        if any(point_in_rect(center_x, center_y, table["bbox"]) for table in tables):
            continue
        text_items.append({"type": "text", **record})

    items = text_items + [
        {"type": "table", "bbox": table["bbox"], "rows": table["rows"]}
        for table in tables
    ]
    items.sort(key=lambda item: (round(item["bbox"][1], 1), item["bbox"][0]))
    raw = page.get_text("text", sort=True)
    return page_number, items, raw, len(tables)


def render_table(rows) -> list[str]:
    if not rows:
        return []

    def markdown_cell(value: str) -> str:
        marker = "\u0000BR\u0000"
        value = escape_inline_html(value.replace("<br>", marker))
        return value.replace(marker, "<br>")

    columns = max(len(row) for row in rows)
    normalized = [row + [""] * (columns - len(row)) for row in rows]
    first_row = tuple(cell.replace("<br>", " ").strip() for cell in normalized[0])
    has_header = first_row in KNOWN_HEADERS
    header = normalized[0] if has_header else [""] * columns
    output = ["|" + "|".join(markdown_cell(cell) for cell in header) + "|"]
    output.append("|" + "|".join(["---"] * columns) + "|")
    start = 1 if has_header else 0
    output.extend(
        "|" + "|".join(markdown_cell(cell) for cell in row) + "|"
        for row in normalized[start:]
    )
    return output


def visible_text(items) -> str:
    """Rebuild textual content before Markdown syntax for completeness checks."""
    parts = []
    for item in items:
        if item["type"] == "text":
            parts.append(item["text"])
        else:
            for row in item["rows"]:
                parts.extend(plain_cell(cell) for cell in row)
    return "\n".join(parts)


def extract_images(document, output: Path, mode: str):
    assets = output.with_name(output.stem + "_assets")
    image_links = defaultdict(list)
    if mode == "none":
        return assets, image_links

    pages = range(document.page_count) if mode == "all" else [0]
    assets.mkdir(parents=True, exist_ok=True)
    seen_xrefs = set()
    for page_number in pages:
        for sequence, image_info in enumerate(document[page_number].get_images(full=True), start=1):
            xref = image_info[0]
            if xref in seen_xrefs:
                continue
            seen_xrefs.add(xref)
            image = document.extract_image(xref)
            name = f"page-{page_number + 1:04d}-{sequence:02d}.{image['ext']}"
            (assets / name).write_bytes(image["image"])
            relative = assets.name + "/" + name
            image_links[page_number].append(f"![PDF 第 {page_number + 1} 页图像 {sequence}]({relative})")
    return assets, image_links


def render_page(page_number, items, headings, image_links):
    output = [f"<!-- PDF 第 {page_number + 1} 页 -->", ""]
    if image_links:
        output.extend(image_links + [""])

    used = set()
    heading_items = {}
    heading_prefixes = set()
    table_headings = {}
    used_table_rows = set()
    matched_headings = 0

    for level, title in headings:
        located = locate_heading(items, title, used)
        if located is not None:
            index, indices = located
            used.update(indices)
            heading_text = " ".join(items[i]["text"].strip() for i in indices)
            heading_items[index] = (level, heading_text)
            heading_prefixes.update(indices[:-1])
            matched_headings += 1
            continue

        target = normalize_heading(title)
        candidates = []
        for item_index, item in enumerate(items):
            if item["type"] != "table":
                continue
            for row_index, row in enumerate(item["rows"]):
                if (item_index, row_index) in used_table_rows:
                    continue
                cells = [plain_cell(cell).strip() for cell in row if cell.strip()]
                row_text = " ".join(cells)
                score = difflib.SequenceMatcher(
                    None, target, normalize_heading(row_text), autojunk=False
                ).ratio()
                candidates.append((score, item_index, row_index, row_text))
        if candidates:
            score, item_index, row_index, row_text = max(candidates)
            if score >= 0.86:
                table_headings[(item_index, row_index)] = (level, row_text)
                used_table_rows.add((item_index, row_index))
                matched_headings += 1

    previous_text = None
    for index, item in enumerate(items):
        if index in heading_prefixes:
            continue
        if item["type"] == "table":
            split_rows = [
                row_index
                for row_index in range(len(item["rows"]))
                if (index, row_index) in table_headings
            ]
            start = 0
            for row_index in split_rows:
                if row_index > start:
                    output.extend(render_table(item["rows"][start:row_index]) + [""])
                level, heading_text = table_headings[(index, row_index)]
                level = max(1, min(level, 6))
                output.extend([f"{'#' * level} {escape_inline_html(heading_text)}", ""])
                start = row_index + 1
            if start < len(item["rows"]):
                output.extend(render_table(item["rows"][start:]) + [""])
            previous_text = None
            continue

        text = item["text"].strip()
        if not text:
            continue
        if index in heading_items:
            level, heading_text = heading_items[index]
            level = max(1, min(level, 6))
            output.extend([f"{'#' * level} {escape_inline_html(heading_text)}", ""])
        else:
            same_block = previous_text is not None and previous_text["block"] == item["block"]
            if same_block and len(output) >= 2:
                output[-2] = output[-2] + "  "
            output.extend([escape_inline_html(text), ""])
        previous_text = item

    return output, matched_headings


def convert_pdf(
    input_pdf: str | Path,
    output_md: str | Path | None = None,
    *,
    workers: int = 4,
    table_strategy: str = "lines_strict",
    images: str = "first",
    strict: bool = True,
    progress: bool = True,
):
    input_pdf = Path(input_pdf).resolve()
    if not input_pdf.is_file():
        raise FileNotFoundError(f"PDF 不存在: {input_pdf}")
    output = Path(output_md).resolve() if output_md else input_pdf.with_suffix(".md")
    output.parent.mkdir(parents=True, exist_ok=True)

    document = pymupdf.open(input_pdf)
    if document.needs_pass:
        raise ValueError("不支持未提供密码的加密 PDF")
    if not any(page.get_text().strip() for page in document):
        raise ValueError("未检测到可提取文字；该 PDF 可能是扫描件，请先进行 OCR")

    headings_by_page = defaultdict(list)
    for level, title, page_number, *_ in document.get_toc(simple=False):
        headings_by_page[page_number - 1].append((level, title))
    expected_headings = sum(map(len, headings_by_page.values()))
    _, image_links = extract_images(document, output, images)

    jobs = [(str(input_pdf), page, table_strategy) for page in range(document.page_count)]
    results = []
    if workers <= 1:
        iterator = map(_analyze_page, jobs)
    else:
        executor = concurrent.futures.ProcessPoolExecutor(max_workers=workers)
        iterator = executor.map(_analyze_page, jobs)
    try:
        for index, result in enumerate(iterator, start=1):
            results.append(result)
            if progress:
                print(f"[{index:>4}/{document.page_count}] 已解析 PDF 第 {index} 页", flush=True)
    finally:
        if workers > 1:
            executor.shutdown()
    results.sort(key=lambda result: result[0])

    output_lines = []
    table_count = 0
    matched_headings = 0
    char_mismatch_pages = []
    order_warning_pages = []
    for page_number, items, raw, count in results:
        table_count += count
        page_lines, matched = render_page(
            page_number,
            items,
            headings_by_page.get(page_number, []),
            image_links.get(page_number, []),
        )
        matched_headings += matched
        output_lines.extend(page_lines)
        if page_number != document.page_count - 1:
            output_lines.extend(['<div style="page-break-after: always;"></div>', ""])

        rebuilt = visible_text(items)
        if compare_chars(raw) != compare_chars(rebuilt):
            missing = compare_chars(raw) - compare_chars(rebuilt)
            extra = compare_chars(rebuilt) - compare_chars(raw)
            char_mismatch_pages.append(
                (page_number + 1, sum(missing.values()), sum(extra.values()))
            )
        if normalize_content(raw) != normalize_content(rebuilt):
            order_warning_pages.append(page_number + 1)

    output.write_text("\n".join(output_lines).rstrip() + "\n", encoding="utf-8", newline="\n")
    report = {
        "input": str(input_pdf),
        "output": str(output),
        "pages": document.page_count,
        "tables": table_count,
        "toc_entries": expected_headings,
        "matched_headings": matched_headings,
        "char_mismatch_pages": char_mismatch_pages,
        "order_warning_pages": order_warning_pages,
    }
    if strict and (char_mismatch_pages or matched_headings != expected_headings):
        raise RuntimeError(
            "严格校验失败: "
            f"char_mismatch_pages={char_mismatch_pages}, "
            f"headings={matched_headings}/{expected_headings}"
        )
    return report


def print_report(report) -> None:
    print(f"[OK] {report['input']}")
    print(f"     -> {report['output']}")
    print(
        f"     pages={report['pages']} tables={report['tables']} "
        f"headings={report['matched_headings']}/{report['toc_entries']} "
        f"char_mismatch_pages={len(report['char_mismatch_pages'])}"
    )
    if report["order_warning_pages"]:
        pages = ",".join(map(str, report["order_warning_pages"][:20]))
        print(f"     reading-order warnings: {pages}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="将文本型 PDF 转换为 Markdown")
    parser.add_argument("input", nargs="?", help="输入 PDF")
    parser.add_argument("output", nargs="?", help="输出 Markdown")
    parser.add_argument("--dir", dest="input_dir", help="批量转换目录中的 PDF")
    parser.add_argument("--out", dest="output_dir", help="批量输出目录")
    parser.add_argument("--workers", type=int, default=4, help="并行页数，默认 4")
    parser.add_argument(
        "--table-strategy",
        default="lines_strict",
        choices=["lines", "lines_strict", "text"],
        help="PyMuPDF 表格检测策略",
    )
    parser.add_argument(
        "--images", default="first", choices=["first", "all", "none"],
        help="提取首页、全部页或不提取图片",
    )
    parser.add_argument("--no-strict", action="store_true", help="校验失败时仍保留输出")
    parser.add_argument("--quiet", action="store_true", help="不输出逐页进度")
    return parser


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    args = build_parser().parse_args()
    common = dict(
        workers=max(1, args.workers),
        table_strategy=args.table_strategy,
        images=args.images,
        strict=not args.no_strict,
        progress=not args.quiet,
    )
    if args.input_dir:
        input_dir = Path(args.input_dir).resolve()
        output_dir = Path(args.output_dir).resolve() if args.output_dir else input_dir
        files = sorted(input_dir.glob("*.pdf"))
        if not files:
            print("目录中没有 PDF 文件", file=sys.stderr)
            return 1
        failures = 0
        for pdf in files:
            try:
                print_report(convert_pdf(pdf, output_dir / (pdf.stem + ".md"), **common))
            except Exception as error:
                failures += 1
                print(f"[FAIL] {pdf}: {error}", file=sys.stderr)
        return 1 if failures else 0
    if not args.input:
        build_parser().print_help()
        return 1
    try:
        print_report(convert_pdf(args.input, args.output, **common))
        return 0
    except Exception as error:
        print(f"[FAIL] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

