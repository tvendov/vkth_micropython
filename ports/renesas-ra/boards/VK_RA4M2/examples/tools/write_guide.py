import os
out = "/home/teodor/agents_guide/RA4M2/git_commit_lessons.md"
os.makedirs(os.path.dirname(out), exist_ok=True)
text = """# Git Commit and Push - Known Difficulties (RA4M2 Project)
# Date: 2026-04-04

## Problem 1: git identity unknown in MSYS2 shell

Symptom:
  fatal: unable to auto-detect email address (got teodor@Teodor_laptop.(none))

Cause: MSYS2 git cannot find user.name / user.email.
  git config --global writes to /home/teodor/.gitconfig inside MSYS2 filesystem.
  Each new MSYS2 shell session may not see it if HOME differs.

Fix: Use LOCAL config (writes to .git/config - persistent for all processes):
  git config user.name tvendov
  git config user.email 68082697+tvendov@users.noreply.github.com

## Problem 2: msys2_shell.cmd hangs / timeout

Symptom: C:/msys_64/msys2_shell.cmd -mingw64 -defterm -no-start -here -c ... times out

Cause: -defterm -no-start -here does not always close the shell after command
  execution - it waits for user input.

FIX: Do NOT use msys2_shell.cmd for one-shot commands!
Use directly:
  C:/msys_64/usr/bin/bash.exe -c "export HOME=/home/teodor; cd /path; commands"

Rules:
  - NO -l flag! (-l = login shell, reads /etc/profile, slow/hangs)
  - Always set HOME explicitly: export HOME=/home/teodor
  - Use ; between commands inside bash string (not &&, safer in PS context)

## Problem 3: PowerShell does not support && operator

Symptom: The token && is not a valid statement separator in this version.
Cause: PowerShell 5 (Windows default) does not support &&.
Fix: Use semicolons inside a bash.exe call, or use PowerShell semicolon ; separator.

## Problem 4: Multi-line commit message hangs in shell

Symptom: git commit -m with multi-line text hangs with >> prompt.
Fix:
  printf 'Subject line\\n\\nBody text\\n' > /tmp/cmsg.txt
  git commit -F /tmp/cmsg.txt

## WORKING GIT PATTERN (VERIFIED 2026-04-04!)

C:/msys_64/usr/bin/bash.exe -c "export HOME=/home/teodor; cd /home/teodor/renesas_micropython; git config user.name tvendov; git config user.email 68082697+tvendov@users.noreply.github.com; git add FILE1 FILE2; git commit -m MESSAGE; git push origin master; echo DONE"

Parameters:
  wait=true
  max_wait_seconds=30 for commit
  max_wait_seconds=90 for push (network dependent)

If timeout: kill process + read-terminal to see result (commit may have succeeded!)

## Build command (RA4M2)
  make BOARD=VK_RA4M2 -j16
  Run in: /home/teodor/renesas_micropython/ports/renesas-ra (MINGW64 terminal)

## GitHub repo
  URL: https://github.com/tvendov/vkth_micropython.git
  Author: tvendov
  Email: 68082697+tvendov@users.noreply.github.com
"""
with open(out, "w") as f:
    f.write(text)
print("FILE_WRITTEN:", out)
