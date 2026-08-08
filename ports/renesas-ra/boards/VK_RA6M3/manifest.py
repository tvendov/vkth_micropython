include("$(PORT_DIR)/boards/manifest.py")
# Networking
require("bundle-networking")

# Vekatech/uPy LVGL demo and display helpers.
freeze("$(BOARD_DIR)/modules", ("Lvg.py", "pRGB.py", "st77xx.py", "lv_utils.py"))
