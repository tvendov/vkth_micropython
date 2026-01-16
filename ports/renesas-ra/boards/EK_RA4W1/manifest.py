# EK_RA4W1 frozen manifest.
#
# Keep the port-level frozen modules, and add EK_RA4W1-specific helpers.

include("$(PORT_DIR)/boards/manifest.py")

# Provide a `bluetooth` Python module that wraps the native `renesas_ble` module.
freeze("$(BOARD_DIR)", ("bluetooth.py",))
