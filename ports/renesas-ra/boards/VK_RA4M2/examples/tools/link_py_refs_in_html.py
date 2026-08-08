from __future__ import annotations

import os
import re
from pathlib import Path


CODE_REF_RE = re.compile(
    r'(<code class="docutils literal notranslate"><span class="pre">(examples/[A-Za-z0-9_./-]+\.py)</span></code>)'
)


def _inside_anchor(text: str, pos: int) -> bool:
    last_open = text.rfind("<a ", 0, pos)
    last_close = text.rfind("</a>", 0, pos)
    return last_open > last_close


def link_file(html_file: Path, examples_root: Path) -> int:
    text = html_file.read_text(encoding="utf-8")
    result = []
    last = 0
    changes = 0

    for match in CODE_REF_RE.finditer(text):
        start, end = match.span()
        result.append(text[last:start])

        if _inside_anchor(text, start):
            result.append(match.group(1))
            last = end
            continue

        example_ref = match.group(2)
        target = examples_root / example_ref.removeprefix("examples/")
        if not target.exists():
            result.append(match.group(1))
            last = end
            continue

        href = Path(os.path.relpath(target, html_file.parent)).as_posix()
        result.append(f'<a class="reference external" href="{href}">{match.group(1)}</a>')
        last = end
        changes += 1

    if changes == 0:
        return 0

    result.append(text[last:])
    html_file.write_text("".join(result), encoding="utf-8")
    return changes


def main() -> None:
    examples_root = Path(__file__).resolve().parent
    html_root = examples_root / "html_project"

    total_files = 0
    total_links = 0
    for html_file in html_root.rglob("*.html"):
        changes = link_file(html_file, examples_root)
        if changes:
            total_files += 1
            total_links += changes

    print(f"updated_files={total_files}")
    print(f"updated_links={total_links}")


if __name__ == "__main__":
    main()
