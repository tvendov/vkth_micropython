import os
total = 0
problem = []
for root, dirs, files in os.walk('.'):
    for fn in sorted(files):
        if not fn.endswith('.py') or fn.startswith('render') or fn == 'index.py' or fn == 'check_comments.py':
            continue
        total += 1
        path = os.path.join(root, fn)
        nocomment = []
        for i, line in enumerate(open(path, encoding='utf-8'), 1):
            s = line.rstrip()
            if not s or s.lstrip().startswith('#'):
                continue
            if '#' not in s:
                nocomment.append(i)
        if nocomment:
            problem.append((path, len(nocomment), nocomment))
print('Total .py files:', total)
print('Files with uncommented code lines:', len(problem))
for p, c, lines in sorted(problem):
    print(f'  {c:3d} lines  {p}')
print('Files fully commented:', total - len(problem))

