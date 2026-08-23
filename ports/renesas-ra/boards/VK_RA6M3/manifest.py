include("$(PORT_DIR)/boards/manifest.py")

# USE_FSP_ETH is intentionally off in the SDR build.  Freezing the networking
# bundle in that configuration is both dead weight and internally inconsistent:
# webrepl references hashlib.sha1 while the no-network qstr scan removes it.
# Restore require("bundle-networking") together with USE_FSP_ETH when LAN returns.

# Vekatech/uPy LVGL demo and display helpers.
freeze("$(BOARD_DIR)/modules", ("Lvg.py", "pRGB.py", "st77xx.py", "lv_utils.py"))
