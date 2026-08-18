#!/bin/sh

export SDCARD_PATH="/mnt/SDCARD"
export USERDATA_PATH="$SDCARD_PATH/.userdata"
export SHARED_USERDATA_PATH="$USERDATA_PATH/shared"

cd $(dirname "$0")
./settings.elf > settings.log 2>&1
