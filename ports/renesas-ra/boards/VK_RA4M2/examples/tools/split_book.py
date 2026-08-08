#!/usr/bin/env python3
"""Split BOOK_BG.md into per-chapter files under book/ directory.

Creates:
  book/index.md          — front matter (title, TOC, resource map)
  book/ch00_setup.md     — Chapter 0
  book/ch01_*.md         — Chapter 1 ... 56
  book/part_XX_intro.md  — Part intros (when they have content before first chapter)
  book/glossary.md       — Glossary
  book/conclusion.md     — Conclusion

Each Part intro is written as a separate file only if it has content
beyond the heading line itself.
"""
import os, re

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "BOOK_BG.md")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "book")
os.makedirs(OUT, exist_ok=True)

# Slug mapping for chapter filenames
def chapter_slug(num, title):
    """Generate a filename slug from chapter number and title."""
    # Transliterate basic Bulgarian for filenames
    tr = {
        'а':'a','б':'b','в':'v','г':'g','д':'d','е':'e','ж':'zh','з':'z',
        'и':'i','й':'y','к':'k','л':'l','м':'m','н':'n','о':'o','п':'p',
        'р':'r','с':'s','т':'t','у':'u','ф':'f','х':'h','ц':'ts','ч':'ch',
        'ш':'sh','щ':'sht','ъ':'a','ь':'','ю':'yu','я':'ya',
    }
    slug = title.lower().strip()
    # Remove difficulty markers
    slug = re.sub(r'`\[.*?\]`', '', slug).strip()
    # Remove backticks and special chars
    slug = slug.replace('`', '').replace('(', '').replace(')', '')
    # Transliterate
    out = []
    for ch in slug:
        if ch in tr:
            out.append(tr[ch])
        elif ch.isascii() and (ch.isalnum() or ch in '-_ '):
            out.append(ch)
        else:
            out.append('_')
    slug = ''.join(out).strip()
    slug = re.sub(r'[\s_]+', '_', slug)
    slug = re.sub(r'_+$', '', slug)
    # Truncate
    slug = slug[:40].rstrip('_')
    return f"ch{num:02d}_{slug}.md"

# Part slug
PART_MAP = {
    'I': '01', 'II': '02', 'III': '03', 'IV': '04', 'IVб': '04b',
    'V': '05', 'VI': '06', 'VII': '07', 'VIII': '08', 'IX': '09',
    'X': '10', 'XI': '11',
}

def part_slug(part_id):
    num = PART_MAP.get(part_id, part_id)
    return f"part_{num}_intro.md"

# Read source
with open(SRC, 'r', encoding='utf-8') as f:
    lines = f.readlines()

# Patterns
re_chapter = re.compile(r'^### Глава (\d+)[.:]\s*(.*)')
re_part = re.compile(r'^## Част ([IVXб]+):\s*(.*)')
re_section = re.compile(r'^## (Цел|Как да|Нива|Съдържание|Ресурсна|Речник|Заключение)')
re_h1 = re.compile(r'^# ')
re_part_x = re.compile(r'^## Част X:')

# Split into segments
segments = []  # (filename, lines[])
current_name = "index.md"
current_lines = []

for line in lines:
    m_ch = re_chapter.match(line)
    m_part = re_part.match(line)
    m_sect = re_section.match(line)

    if m_ch:
        # Save current
        if current_lines:
            segments.append((current_name, current_lines))
        num = int(m_ch.group(1))
        title = m_ch.group(2)
        current_name = chapter_slug(num, title)
        current_lines = [line]
    elif m_part:
        if current_lines:
            segments.append((current_name, current_lines))
        part_id = m_part.group(1)
        current_name = part_slug(part_id)
        current_lines = [line]
    elif m_sect:
        sect = m_sect.group(1)
        if current_lines:
            segments.append((current_name, current_lines))
        if sect == 'Речник':
            current_name = "glossary.md"
        elif sect == 'Заключение':
            current_name = "conclusion.md"
        else:
            current_name = "index.md"  # front matter sections go to index
        # Check if we're appending to existing index
        existing = [s for s in segments if s[0] == current_name]
        if existing and current_name == "index.md":
            # Append to existing index segment
            existing[-1][1].append('\n')
            existing[-1][1].append(line)
            current_lines = existing[-1][1]
            segments = [s for s in segments if s[0] != current_name or s is existing[-1]]
            continue
        current_lines = [line]
    elif re_h1.match(line):
        # Book title — goes to index
        current_lines.append(line)
    elif re_part_x.match(line):
        # Part X (special — no roman numeral in standard pattern)
        if current_lines:
            segments.append((current_name, current_lines))
        current_name = "part_10_intro.md"
        current_lines = [line]
    else:
        current_lines.append(line)

# Don't forget last segment
if current_lines:
    segments.append((current_name, current_lines))

# Merge duplicate index segments
index_parts = [s for s in segments if s[0] == "index.md"]
other_parts = [s for s in segments if s[0] != "index.md"]
if len(index_parts) > 1:
    merged = []
    for ip in index_parts:
        merged.extend(ip[1])
    segments = [("index.md", merged)] + other_parts
else:
    segments = index_parts + other_parts

# Write files
file_order = []
for fname, content in segments:
    path = os.path.join(OUT, fname)
    with open(path, 'w', encoding='utf-8') as f:
        # Strip leading/trailing blank lines
        text = ''.join(content).strip() + '\n'
        f.write(text)
    n_lines = text.count('\n')
    print(f"  {fname:50s} {n_lines:5d} lines")
    if fname not in file_order:
        file_order.append(fname)

# Generate manifest (for md2pdf.py to use)
manifest_path = os.path.join(OUT, "MANIFEST.txt")
with open(manifest_path, 'w', encoding='utf-8') as f:
    for fname in file_order:
        f.write(fname + '\n')
print(f"\n  MANIFEST.txt written with {len(file_order)} files")

# Summary
total_lines = sum(''.join(s[1]).count('\n') for s in segments)
print(f"\n  Total: {len(segments)} files, {total_lines} lines")
print(f"  Output: {OUT}")
