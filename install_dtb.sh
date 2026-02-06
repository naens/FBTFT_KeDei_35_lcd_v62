#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

dtc -@ -I dts -O dtb -o "$SCRIPT_DIR/kedei.dtbo" "$SCRIPT_DIR/kedei.dts"

# Detect boot partition location (Bookworm uses /boot/firmware/)
if [ -d /boot/firmware/overlays ]; then
	OVERLAY_DIR=/boot/firmware/overlays
else
	OVERLAY_DIR=/boot/overlays
fi

cp "$SCRIPT_DIR/kedei.dtbo" "$OVERLAY_DIR/"

echo "Overlay installed to $OVERLAY_DIR/kedei.dtbo"
echo "Ensure these lines are in your config.txt:"
echo "  dtparam=spi=on"
echo "  dtoverlay=kedei"

