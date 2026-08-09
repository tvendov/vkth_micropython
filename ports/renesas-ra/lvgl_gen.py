import os
import runpy
import sys


generator = sys.argv.pop(1)
generator = os.path.abspath(generator)
binding_dir = os.path.dirname(os.path.dirname(generator))
generator_dir = os.path.dirname(generator)
sys.path.insert(0, generator_dir)

# Upstream appends preprocessor output with >>. Remove the previous generated
# file so repeated Make invocations do not parse duplicate declarations.
output_path = None
for index, argument in enumerate(sys.argv):
    if argument.startswith("--output="):
        output_path = argument.split("=", 1)[1]
        break
    if argument == "--output" and index + 1 < len(sys.argv):
        output_path = sys.argv[index + 1]
        break

if output_path:
    preprocessed_path = os.path.splitext(output_path)[0] + ".pp"
    if os.path.exists(preprocessed_path):
        os.remove(preprocessed_path)

# The upstream generator uses both the public and private LVGL APIs when it
# identifies callback user-data storage. Its normal top-level build creates
# this combined header before invoking the MicroPython build.
lvgl_dir = os.path.join(binding_dir, "lib", "lvgl")
header_dir = os.path.join(binding_dir, "build")
header_path = os.path.join(header_dir, "lvgl_header.h")
os.makedirs(header_dir, exist_ok=True)
with open(header_path, "w", newline="\n") as header:
    header.write('#include "{}"\n'.format(os.path.join(lvgl_dir, "lvgl.h").replace("\\", "/")))
    header.write('#include "{}"\n'.format(os.path.join(lvgl_dir, "src", "lvgl_private.h").replace("\\", "/")))

# The MSYS2 Python runtime reports win32, but the Renesas build uses GCC.
if sys.platform.startswith("win"):
    sys.platform = "linux"

runpy.run_path(generator, run_name="__main__")
