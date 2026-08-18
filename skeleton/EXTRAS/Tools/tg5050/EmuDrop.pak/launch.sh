#!/bin/sh
APP_DIR=$(dirname "$0")
cd "$APP_DIR"

# Ensure permissions
chmod +x app_ota.sh db_ota.sh EmuDrop assets/infoscreen.sh

# Logging setup
LOG_FILE="$APP_DIR/log.txt"
echo "Starting EmuDrop at $(date)" > "$LOG_FILE"
echo "Current directory: $PWD" >> "$LOG_FILE"

# Set library paths for NextUI
export LD_LIBRARY_PATH="$APP_DIR/lib:$LD_LIBRARY_PATH:/usr/lib:/lib"

# PySDL2 Path Setup
# Check for local lib folder first
if [ -d "$APP_DIR/lib" ] && [ "$(ls -A $APP_DIR/lib)" ]; then
    echo "Found local lib directory with files." >> "$LOG_FILE"
    export PYSDL2_DLL_PATH="$APP_DIR/lib"
elif [ -d "/usr/trimui/lib" ]; then
    echo "Found /usr/trimui/lib, using it for PySDL2." >> "$LOG_FILE"
    export PYSDL2_DLL_PATH="/usr/trimui/lib"
else
    echo "No specific lib dir found, defaulting to /usr/lib" >> "$LOG_FILE"
    export PYSDL2_DLL_PATH="/usr/lib"
fi

echo "PYSDL2_DLL_PATH: $PYSDL2_DLL_PATH" >> "$LOG_FILE"
echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH" >> "$LOG_FILE"

# Environment Variables
export ROMS_DIR="/mnt/SDCARD/Roms/"
export IMGS_DIR="/mnt/SDCARD/Roms/{SYSTEM}/.media/{IMAGE_NAME}.png"
export EXECUTABLES_DIR="$APP_DIR/assets/executables/"
export INFOSCREEN="/mnt/SDCARD/System/usr/trimui/scripts/infoscreen.sh"

if [ -f "$APP_DIR/assets/systems_nextui.json" ]; then
    cp "$APP_DIR/assets/systems_nextui.json" "$APP_DIR/assets/systems.json"
fi

# Show splash screen using NextUI show.elf
if [ -f "$APP_DIR/icon.png" ]; then
    # NextUI show.elf takes image path and delay in seconds
    if command -v show.elf >/dev/null 2>&1; then
        show.elf "$APP_DIR/icon.png" 1
    fi
fi

# Internet Check
if [ -f "$INFOSCREEN" ]; then
    $INFOSCREEN -m "Checking internet connection..." -t 0.2
    
    if ping -c 1 8.8.8.8 > /dev/null 2>&1; then
        $INFOSCREEN -m "Internet connection detected." -t 0.1
    else 
        $INFOSCREEN -m "No internet connection. Press B to exit." -k B
        exit
    fi
fi

# CPU Power Management: Use ondemand governor and set 408MHz minimum frequency floor
if [ -f "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor" ]; then
    echo ondemand > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true
fi
if [ -f "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq" ]; then
    echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq 2>/dev/null || true
fi
if [ -d "/sys/devices/system/cpu/cpufreq/ondemand" ]; then
    echo 85 > /sys/devices/system/cpu/cpufreq/ondemand/up_threshold 2>/dev/null || true
fi
# Network TCP Buffer & Performance Tuning for Fast WiFi Downloads
if [ -f "/proc/sys/net/core/rmem_max" ]; then
    echo 4194304 > /proc/sys/net/core/rmem_max 2>/dev/null || true
fi
if [ -f "/proc/sys/net/core/wmem_max" ]; then
    echo 4194304 > /proc/sys/net/core/wmem_max 2>/dev/null || true
fi
if [ -f "/proc/sys/net/ipv4/tcp_rmem" ]; then
    echo "4096 87380 4194304" > /proc/sys/net/ipv4/tcp_rmem 2>/dev/null || true
fi
if [ -f "/proc/sys/net/ipv4/tcp_wmem" ]; then
    echo "4096 65536 4194304" > /proc/sys/net/ipv4/tcp_wmem 2>/dev/null || true
fi
if [ -f "/proc/sys/net/ipv4/tcp_window_scaling" ]; then
    echo 1 > /proc/sys/net/ipv4/tcp_window_scaling 2>/dev/null || true
fi

# Launch app (OTA updates disabled for custom build)
echo "Launching EmuDrop binary..." >> "$LOG_FILE"
./EmuDrop >> "$LOG_FILE" 2>&1
EXIT_CODE=$?
echo "EmuDrop exited with code $EXIT_CODE" >> "$LOG_FILE"

