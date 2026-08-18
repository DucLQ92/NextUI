#!/bin/sh

SDCARD_PATH=/mnt/SDCARD

# --------------------------------------
# migration code here
# --------------------------------------
if [ -f "${SDCARD_PATH}/.system/tg5050/bin/show2.elf" ]; then
	mkdir -p "${SDCARD_PATH}/.tmp_update/tg5050"
	cp "${SDCARD_PATH}/.system/tg5050/bin/show2.elf" "${SDCARD_PATH}/.tmp_update/tg5050/show2.elf" 2>/dev/null || true
fi
