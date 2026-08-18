#!/bin/sh

cd $(dirname "$0")

show2.elf --mode=simple --image "$SDCARD_PATH/.system/res/logo.png" --text="Tính năng đang phát triển..." --timeout=3
