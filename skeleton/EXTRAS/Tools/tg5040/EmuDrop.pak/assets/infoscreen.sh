#!/bin/sh

IMAGE="$1"

if [ -z "$IMAGE" ]; then
    exit 1
fi

# Verify image exists
if [ ! -f "$IMAGE" ]; then
	exit 1
fi

# Try multiple display methods for TrimUI
if [ -x "/mnt/SDCARD/System/usr/trimui/bin/show" ]; then
	/mnt/SDCARD/System/usr/trimui/bin/show "$IMAGE"
elif [ -x "/usr/trimui/bin/show" ]; then
	/usr/trimui/bin/show "$IMAGE"
elif command -v show >/dev/null 2>&1; then
	show "$IMAGE"
elif command -v fbv >/dev/null 2>&1; then
	fbv -e "$IMAGE"
elif command -v fbi >/dev/null 2>&1; then
	fbi -a -noverbose -t 1 "$IMAGE" </dev/tty0 2>/dev/null
