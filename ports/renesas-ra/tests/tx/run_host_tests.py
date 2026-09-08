#!/usr/bin/env python3
"""Compile and execute the actual portable TX C core, without target hardware.

Usage: python run_host_tests.py [--cc /path/to/gcc]
CC may name a GCC/Clang-compatible compiler executable (not a shell command).
Temporary binaries are kept outside the source tree and removed on completion.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC"))
    args = parser.parse_args()
    compiler = args.cc or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        parser.error("No C compiler found; pass --cc with a GCC or Clang executable.")

    port = Path(__file__).resolve().parents[2]
    environment = os.environ.copy()
    resolved_compiler = shutil.which(compiler) or compiler
    compiler_directory = str(Path(resolved_compiler).resolve().parent)
    environment["PATH"] = compiler_directory + os.pathsep + environment.get("PATH", "")

    with tempfile.TemporaryDirectory(prefix="ra-tx-core-") as temporary:
        for suite, extra in (("test_tx_core", []), ("test_tone", [str(port / "ra" / "ra_tone.c")])):
            binary = Path(temporary) / (suite + (".exe" if os.name == "nt" else ""))
            command = [
                compiler,
                "-std=c11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(port / "ra"),
                str(port / "ra" / "ra_tx_core.c"),
                str(Path(__file__).with_name(suite + ".c")),
                "-lm",
                "-o",
                str(binary),
            ] + extra
            print("Compiling", suite, "with:", compiler, flush=True)
            subprocess.run(command, check=True, env=environment)
            subprocess.run([str(binary)], check=True, env=environment)
    print("Host C math/address-model tests only; target registers, ISR counts and RF are NOT VERIFIED.")


if __name__ == "__main__":
    main()
