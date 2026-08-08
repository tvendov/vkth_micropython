"""render_book_html.py — Convert BOOK_BG.md to a styled standalone HTML file.

Compatible with link_py_refs_in_html.py: inline code is rendered as
  <code class="docutils literal notranslate"><span class="pre">...</span></code>
so that script can auto-link examples/*.py references.

Usage:
    python render_book_html.py
Outputs: BOOK_BG.html (same directory as this script)
"""

from __future__ import annotations

import html
import re
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent
BOOK_MARKDOWN_PATH = ROOT_DIR / "BOOK_BG.md"
BOOK_HTML_PATH = ROOT_DIR / "BOOK_BG.html"
BOOK_TITLE = "Книга: MicroPython за VK_RA4M2 чрез демонстрационни примери"

CSS = """
*, *::before, *::after { box-sizing: border-box; }
body {
    font-family: Arial, "Helvetica Neue", sans-serif;
    font-size: 11pt;
    color: #111;
    background: #fff;
    max-width: 960px;
    margin: 0 auto;
    padding: 2em 2.5em;
    line-height: 1.65;
}
h1 { font-size: 1.75em; margin: 0.4em 0 0.3em; color: #0a0a0a; }
h2 { font-size: 1.35em; margin: 1.6em 0 0.4em; border-bottom: 2px solid #ddd;
     padding-bottom: 0.25em; color: #1a1a1a; }
h3 { font-size: 1.1em; margin: 1.2em 0 0.3em; color: #1a1a1a; }
h4 { font-size: 1em; margin: 0.9em 0 0.25em; color: #222; }
p  { margin: 0.4em 0; }
ul, ol { margin: 0.35em 0; padding-left: 1.6em; }
li { margin-bottom: 0.2em; }
pre {
    background: #1e1e1e;
    color: #d4d4d4;
    padding: 0.9em 1.1em;
    border-radius: 5px;
    overflow-x: auto;
    font-family: Consolas, "Courier New", monospace;
    font-size: 9.5pt;
    line-height: 1.4;
    margin: 0.7em 0;
}
pre code { background: none; padding: 0; color: inherit; border-radius: 0;
           font-size: inherit; }
code.docutils {
    font-family: Consolas, "Courier New", monospace;
    background: #f0f0f0;
    border: 1px solid #ddd;
    padding: 1px 4px;
    border-radius: 3px;
    font-size: 0.93em;
    color: #c7254e;
    white-space: nowrap;
}
blockquote {
    border-left: 4px solid #f0a500;
    margin: 0.8em 0;
    padding: 0.5em 1em;
    background: #fffbf0;
    color: #333;
    border-radius: 0 4px 4px 0;
}
blockquote p { margin: 0.2em 0; }
blockquote.warning { border-color: #cc3300; background: #fff5f5; }
table {
    border-collapse: collapse;
    margin: 0.8em 0;
    font-size: 0.95em;
    width: auto;
}
th, td {
    border: 1px solid #ccc;
    padding: 0.35em 0.7em;
    text-align: left;
}
th { background: #f5f5f5; font-weight: bold; }
tr:nth-child(even) td { background: #fafafa; }
a { color: #0055cc; text-decoration: none; }
a:hover { text-decoration: underline; }
strong { color: #111; }
hr { border: none; border-top: 1px solid #ddd; margin: 1.5em 0; }
.toc h3 a, .toc li a { color: #0055cc; }
"""


# ---------------------------------------------------------------------------
# Inline markup
# ---------------------------------------------------------------------------

def _apply_bold_italic(text: str) -> str:
    """Apply **bold** and *italic* after HTML-escaping (order matters)."""
    # Bold (double asterisk)
    text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
    # Italic (single asterisk, not part of double)
    text = re.sub(r"(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*)", r"<em>\1</em>", text)
    return text


def apply_inline(text: str) -> str:
    """Convert inline backtick code spans, bold, italic; HTML-escape the rest."""
    parts: list[tuple[str, str]] = []  # (kind, content)
    current: list[str] = []
    in_code = False
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "`":
            parts.append(("text" if not in_code else "code", "".join(current)))
            current = []
            in_code = not in_code
        else:
            current.append(ch)
        i += 1
    parts.append(("text" if not in_code else "code", "".join(current)))

    result: list[str] = []
    for kind, content in parts:
        if kind == "code":
            escaped = html.escape(content)
            result.append(
                f'<code class="docutils literal notranslate">'
                f'<span class="pre">{escaped}</span></code>'
            )
        else:
            escaped = html.escape(content)
            escaped = _apply_bold_italic(escaped)
            result.append(escaped)
    return "".join(result)


# ---------------------------------------------------------------------------
# Heading / anchor helpers
# ---------------------------------------------------------------------------

def parse_heading(line: str) -> tuple[int | None, str | None]:
    for level, prefix in [(4, "#### "), (3, "### "), (2, "## "), (1, "# ")]:
        if line.startswith(prefix):
            return level, line[len(prefix):].strip()
    return None, None


def collect_heading_anchors(markdown_text: str) -> dict[str, str]:
    """Return {heading_text: anchor_id} for all non-TOC headings."""
    anchors: dict[str, str] = {}
    idx = 1
    current_h2: str | None = None
    for raw in markdown_text.splitlines():
        level, text = parse_heading(raw)
        if text is None:
            continue
        if level == 2:
            current_h2 = text
        if current_h2 == "Съдържание" and level == 3:
            continue
        anchor = f"section-{idx}"
        anchors.setdefault(text, anchor)
        idx += 1
    return anchors


# ---------------------------------------------------------------------------
# Table parsing
# ---------------------------------------------------------------------------

def is_table_sep(line: str) -> bool:
    return bool(re.match(r"^\|[-| :]+\|?\s*$", line))


def render_table(rows: list[str]) -> str:
    # rows[0] = header, rows[1] = separator, rows[2:] = data
    def split_row(row: str) -> list[str]:
        row = row.strip()
        if row.startswith("|"):
            row = row[1:]
        if row.endswith("|"):
            row = row[:-1]
        return [c.strip() for c in row.split("|")]

    if len(rows) < 2:
        return ""
    header_cells = split_row(rows[0])
    out = ["<table>\n<thead><tr>"]
    for cell in header_cells:
        out.append(f"<th>{apply_inline(cell)}</th>")
    out.append("</tr></thead>\n<tbody>")
    for row in rows[2:]:
        cells = split_row(row)
        # pad or trim to match header width
        while len(cells) < len(header_cells):
            cells.append("")
        out.append("<tr>")
        for cell in cells[: len(header_cells)]:
            out.append(f"<td>{apply_inline(cell)}</td>")
        out.append("</tr>")
    out.append("</tbody></table>\n")
    return "".join(out)


# ---------------------------------------------------------------------------
# Main converter
# ---------------------------------------------------------------------------

def convert(markdown_text: str) -> str:
    heading_anchors = collect_heading_anchors(markdown_text)
    lines = markdown_text.splitlines()
    out: list[str] = []

    # State
    in_code = False
    code_lang = ""
    code_lines: list[str] = []
    in_ul = False
    in_ol = False
    in_blockquote = False
    blockquote_lines: list[str] = []
    table_rows: list[str] = []
    current_h2: str | None = None
    anchor_counter = [1]  # mutable for closure
    toc_h2_tracker = [None]  # tracks h2 during anchor assignment

    def next_anchor(level: int, text: str) -> str | None:
        if level == 2:
            toc_h2_tracker[0] = text
        if toc_h2_tracker[0] == "Съдържание" and level == 3:
            return None
        a = f"section-{anchor_counter[0]}"
        anchor_counter[0] += 1
        return a

    def close_list() -> None:
        nonlocal in_ul, in_ol
        if in_ul:
            out.append("</ul>\n")
            in_ul = False
        if in_ol:
            out.append("</ol>\n")
            in_ol = False

    def flush_blockquote() -> None:
        nonlocal in_blockquote, blockquote_lines
        if not blockquote_lines:
            in_blockquote = False
            return
        content = " ".join(blockquote_lines).strip()
        css_class = "warning" if "⚠" in content else ""
        cls = f' class="{css_class}"' if css_class else ""
        out.append(f"<blockquote{cls}><p>{apply_inline(content)}</p></blockquote>\n")
        blockquote_lines = []
        in_blockquote = False

    def flush_table() -> None:
        nonlocal table_rows
        if table_rows:
            out.append(render_table(table_rows))
            table_rows = []

    for raw in lines:
        # --- code block ---
        if raw.strip().startswith("```"):
            flush_table()
            flush_blockquote()
            close_list()
            if not in_code:
                code_lang = raw.strip()[3:].strip()
                in_code = True
                code_lines = []
            else:
                content = html.escape("\n".join(code_lines))
                lang_cls = f' class="language-{html.escape(code_lang)}"' if code_lang else ""
                out.append(f"<pre><code{lang_cls}>{content}</code></pre>\n")
                in_code = False
            continue

        if in_code:
            code_lines.append(raw)
            continue

        # --- blockquote ---
        if raw.startswith(">"):
            flush_table()
            close_list()
            in_blockquote = True
            blockquote_lines.append(raw[1:].strip())
            continue

        if in_blockquote:
            flush_blockquote()

        # --- table row ---
        if raw.startswith("|"):
            close_list()
            table_rows.append(raw)
            continue

        if table_rows:
            flush_table()

        # --- blank line ---
        if raw.strip() == "":
            close_list()
            out.append("\n")
            continue

        # --- heading ---
        level, heading_text = parse_heading(raw)
        if heading_text is not None:
            close_list()
            if level == 2:
                current_h2 = heading_text
            is_toc = current_h2 == "Съдържание"
            anchor = next_anchor(level, heading_text)

            tag = f"h{level}"
            if is_toc and anchor is None and level == 3:
                target = heading_anchors.get(heading_text)
                inner = (
                    f'<a href="#{target}">{html.escape(heading_text)}</a>'
                    if target
                    else html.escape(heading_text)
                )
            else:
                inner = apply_inline(heading_text)

            id_attr = f' id="{anchor}"' if anchor else ""
            out.append(f"<{tag}{id_attr}>{inner}</{tag}>\n")
            continue

        # --- bullet list ---
        if raw.startswith("- ") or raw.startswith("  - "):
            indent = len(raw) - len(raw.lstrip())
            item_text = raw.lstrip()[2:].strip()
            if not in_ul:
                if in_ol:
                    out.append("</ol>\n")
                    in_ol = False
                out.append("<ul>\n")
                in_ul = True
            is_toc = current_h2 == "Съдържание"
            if is_toc:
                target = heading_anchors.get(item_text)
                inner = (
                    f'<a href="#{target}">{html.escape(item_text)}</a>'
                    if target
                    else apply_inline(item_text)
                )
            else:
                inner = apply_inline(item_text)
            out.append(f"<li>{inner}</li>\n")
            continue

        # --- numbered list ---
        m = re.match(r"^(\s*)(\d+)\.\s+(.*)", raw)
        if m:
            item_text = m.group(3).strip()
            if not in_ol:
                if in_ul:
                    out.append("</ul>\n")
                    in_ul = False
                out.append("<ol>\n")
                in_ol = True
            is_toc = current_h2 == "Съдържание"
            if is_toc:
                target = heading_anchors.get(item_text)
                inner = (
                    f'<a href="#{target}">{html.escape(item_text)}</a>'
                    if target
                    else apply_inline(item_text)
                )
            else:
                inner = apply_inline(item_text)
            out.append(f"<li>{inner}</li>\n")
            continue

        # --- horizontal rule ---
        if re.match(r"^-{3,}\s*$", raw) or re.match(r"^={3,}\s*$", raw):
            close_list()
            out.append("<hr>\n")
            continue

        # --- paragraph ---
        close_list()
        out.append(f"<p>{apply_inline(raw.strip())}</p>\n")

    close_list()
    flush_blockquote()
    flush_table()

    return "".join(out)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def render_html(
    markdown_path: Path = BOOK_MARKDOWN_PATH,
    html_path: Path = BOOK_HTML_PATH,
    title: str = BOOK_TITLE,
) -> None:
    markdown_path = Path(markdown_path)
    html_path = Path(html_path)
    text = markdown_path.read_text(encoding="utf-8")
    body = convert(text)
    doc = (
        "<!DOCTYPE html>\n"
        '<html lang="bg">\n'
        "<head>\n"
        '<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
        f"<title>{html.escape(title)}</title>\n"
        "<style>\n"
        f"{CSS}"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        f"{body}"
        "</body>\n"
        "</html>\n"
    )
    html_path.write_text(doc, encoding="utf-8")
    print(html_path)


if __name__ == "__main__":
    render_html()
