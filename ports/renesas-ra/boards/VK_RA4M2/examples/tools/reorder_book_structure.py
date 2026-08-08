from __future__ import annotations

import re
from pathlib import Path


BOOK_PATH = Path(__file__).with_name("BOOK_BG.md")

PART_GROUPS = [
    [1, 2],
    [3, 4, 5, 6, 7, 8, 9],
    [10, 11, 12, 13],
    [14, 15, 16, 17, 18],
    [19, 20],
    [21, 22, 23, 24, 25, 26],
    [27, 28, 29, 30, 31, 32, 33, 34],
    [35, 36, 37],
    [38, 39, 40],
    [41, 42, 43, 44, 45, 46],
    [47, 48],
    [49],
    [50, 51, 52, 53, 54],
]

CHAPTER_RE = re.compile(r"^###\s+.*?(\d+)\.\s+(.+)$")


def normalized_line(line: str) -> str:
    return line.lstrip("\ufeff").rstrip("\n")


def find_body_bounds(lines: list[str]) -> tuple[int, int]:
    h2_indices = [i for i, line in enumerate(lines) if normalized_line(line).startswith("## ")]
    chapter_indices = [i for i, line in enumerate(lines) if CHAPTER_RE.match(normalized_line(line))]

    if not h2_indices or not chapter_indices:
        raise ValueError("Could not find the expected part/chapter structure in BOOK_BG.md")

    first_chapter = chapter_indices[0]
    last_chapter = chapter_indices[-1]

    body_start = max(i for i in h2_indices if i < first_chapter)

    try:
        catalog_idx = next(i for i in h2_indices if i > last_chapter)
    except StopIteration as exc:
        raise ValueError("Could not find the catalog section after the last chapter") from exc

    return body_start, catalog_idx


def extract_chapter_blocks(body_lines: list[str]) -> dict[int, str]:
    chapter_indices = [i for i, line in enumerate(body_lines) if CHAPTER_RE.match(normalized_line(line))]
    part_indices = [i for i, line in enumerate(body_lines) if normalized_line(line).startswith("## ")]

    chapters: dict[int, str] = {}
    for pos, start in enumerate(chapter_indices):
        header = normalized_line(body_lines[start])
        match = CHAPTER_RE.match(header)
        if not match:
            raise ValueError(f"Unrecognized chapter heading: {header!r}")
        number = int(match.group(1))
        next_chapter = chapter_indices[pos + 1] if pos + 1 < len(chapter_indices) else len(body_lines)
        next_part = next((idx for idx in part_indices if idx > start), len(body_lines))
        end = min(next_chapter, next_part)
        chapters[number] = "".join(body_lines[start:end]).rstrip() + "\n\n"

    expected = set(range(1, 55))
    if set(chapters) != expected:
        missing = sorted(expected - set(chapters))
        extra = sorted(set(chapters) - expected)
        raise ValueError(f"Chapter parse mismatch: missing={missing}, extra={extra}")

    return chapters


def extract_part_blocks(body_lines: list[str]) -> dict[int, str]:
    chapter_positions = {
        int(CHAPTER_RE.match(normalized_line(line)).group(1)): idx
        for idx, line in enumerate(body_lines)
        if CHAPTER_RE.match(normalized_line(line))
    }
    part_indices = [i for i, line in enumerate(body_lines) if normalized_line(line).startswith("## ")]

    part_blocks: dict[int, str] = {}
    for group in PART_GROUPS:
        first_chapter = group[0]
        chapter_idx = chapter_positions[first_chapter]
        part_idx = max(i for i in part_indices if i < chapter_idx)
        part_blocks[first_chapter] = "".join(body_lines[part_idx:chapter_idx]).rstrip() + "\n\n"

    return part_blocks


def build_body(chapter_blocks: dict[int, str], part_blocks: dict[int, str]) -> str:
    sections: list[str] = []
    for group in PART_GROUPS:
        block_parts = [part_blocks[group[0]]]
        for chapter in group:
            block_parts.append(chapter_blocks[chapter])
        sections.append("".join(block_parts).rstrip() + "\n")
    return "\n".join(sections) + "\n"


def main() -> None:
    lines = BOOK_PATH.read_text(encoding="utf-8").splitlines(keepends=True)
    body_start, catalog_idx = find_body_bounds(lines)

    prefix = "".join(lines[:body_start])
    body_lines = lines[body_start:catalog_idx]
    tail = "".join(lines[catalog_idx:])

    chapter_blocks = extract_chapter_blocks(body_lines)
    part_blocks = extract_part_blocks(body_lines)
    rebuilt = prefix + build_body(chapter_blocks, part_blocks) + tail

    BOOK_PATH.write_text(rebuilt, encoding="utf-8")
    print(f"normalized {BOOK_PATH}")


if __name__ == "__main__":
    main()
