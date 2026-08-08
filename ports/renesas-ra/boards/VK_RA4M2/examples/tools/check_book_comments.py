import re

with open("BOOK_BG.md", encoding="utf-8") as f:
    lines = f.readlines()

in_python = False
block_start = 0
total_blocks = 0
problem_blocks = []

for i, line in enumerate(lines, 1):
    stripped = line.rstrip()
    if stripped == "```python":
        in_python = True
        block_start = i
        block_lines = []
        continue
    if stripped == "```" and in_python:
        in_python = False
        total_blocks += 1
        nocomment = []
        for ln, code in block_lines:
            s = code.rstrip()
            if not s or s.lstrip().startswith("#"):
                continue
            if "#" not in s:
                nocomment.append((ln, s))
        if nocomment:
            problem_blocks.append((block_start, nocomment))
        continue
    if in_python:
        block_lines.append((i, line))

print(f"Total python blocks in BOOK_BG.md: {total_blocks}")
print(f"Blocks with uncommented code lines: {len(problem_blocks)}")
for start, nocomment in problem_blocks:
    print(f"\n  Block starting at line {start}: {len(nocomment)} uncommented lines")
    for ln, code in nocomment:
        print(f"    line {ln}: {code.rstrip()}")
print(f"\nFully commented blocks: {total_blocks - len(problem_blocks)}")

