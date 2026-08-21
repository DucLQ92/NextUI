#!/bin/sh
# Troubleshooting log collection, driven by the "Debug logging" setting in
# Settings > System. Sourced by MinUI.pak; every function is a no-op unless the
# setting is on, so the default boot path is unchanged.
#
# What this is for: a spontaneous reboot leaves no trace by default. NextUI
# already logs the interesting transitions ("suspending to RAM", "returned from
# suspend"), but MinUI.pak truncates that log every time the launcher restarts,
# so a crash destroys its own evidence on the way back up. With the setting on,
# each boot gets its own session directory that nothing later overwrites.
#
# Kernel-side evidence is collected too, because a reboot out of sleep is more
# likely to be a kernel or PMIC event than anything NextUI can see. The only
# source that reliably survives a reboot is a persistent store the kernel writes
# at panic time -- if this device has one, logs_begin_session harvests it, and
# 00-boot.txt records whether it was there at all.

LOGS_ENABLED=0
LOGS_SESSION_DIR=""
LOGS_NEXTUI_LOG=""
LOGS_HEARTBEAT_PID=""

# Keep two days' worth, and independently cap the count: pruning by age alone is
# only as good as the clock, and this device restores its time from a file.
LOGS_KEEP_DAYS=1        # find -mtime +1 == older than ~2 days
LOGS_KEEP_SESSIONS=20

logs_enabled() {
    [ "$LOGS_ENABLED" = 1 ]
}

logs_stamp() {
    date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo "unknown-time"
}

# Append a timestamped line to the session log. Safe to call when disabled.
logs_mark() {
    logs_enabled || return 0
    printf '[%s] %s\n' "$(logs_stamp)" "$1" >> "$LOGS_SESSION_DIR/00-boot.txt"
}

logs_prune() {
    # By age first...
    find "$LOGS_PATH" -maxdepth 1 -name 'session-*' -type d -mtime +$LOGS_KEEP_DAYS \
        -exec rm -rf {} \; 2>/dev/null
    # ...then by count, so a wrong clock cannot let these accumulate forever.
    # Session names sort chronologically, so the oldest are the first listed.
    _count=$(ls -1d "$LOGS_PATH"/session-* 2>/dev/null | wc -l)
    if [ "$_count" -gt "$LOGS_KEEP_SESSIONS" ]; then
        ls -1d "$LOGS_PATH"/session-* 2>/dev/null \
            | head -n $((_count - LOGS_KEEP_SESSIONS)) \
            | while read -r _dir; do rm -rf "$_dir"; done
    fi
}

# Copy whatever the kernel managed to persist across the last reboot. These are
# cleared after copying: the persistent store is a small fixed region, and a full
# one silently stops recording, so leaving an old crash there would cost us the
# next one.
logs_harvest_previous_boot() {
    _found=""

    if [ -d /sys/fs/pstore ] && [ -n "$(ls -A /sys/fs/pstore 2>/dev/null)" ]; then
        mkdir -p "$LOGS_SESSION_DIR/01-previous-boot"
        cp -f /sys/fs/pstore/* "$LOGS_SESSION_DIR/01-previous-boot/" 2>/dev/null
        rm -f /sys/fs/pstore/* 2>/dev/null
        _found="pstore"
    fi

    if [ -f /proc/last_kmsg ]; then
        mkdir -p "$LOGS_SESSION_DIR/01-previous-boot"
        cp -f /proc/last_kmsg "$LOGS_SESSION_DIR/01-previous-boot/last_kmsg.txt" 2>/dev/null
        _found="${_found:+$_found }last_kmsg"
    fi

    if [ -n "$_found" ]; then
        logs_mark "previous boot evidence collected: $_found"
    else
        logs_mark "no previous boot evidence (kernel has no persistent store, or it was a clean reboot)"
    fi
}

# One line a minute. This is not padding: it is how you find out *when* the
# device died, since a spontaneous reboot leaves no end marker. The sync is the
# point of it -- without one, the last writes sit in the page cache and are lost
# in exactly the crash we are trying to record.
logs_heartbeat() {
    _ticks=0
    while true; do
        sleep 60
        _ticks=$((_ticks + 1))
        printf '[%s] alive, uptime %s\n' "$(logs_stamp)" \
            "$(cut -d' ' -f1 /proc/uptime 2>/dev/null)" \
            >> "$LOGS_SESSION_DIR/03-heartbeat.txt"
        # Re-snapshot the kernel ring buffer every ten minutes. Overwritten
        # rather than appended, so it stays bounded while still holding whatever
        # the kernel said most recently before a crash.
        if [ $((_ticks % 10)) = 0 ]; then
            dmesg > "$LOGS_SESSION_DIR/04-dmesg-latest.txt" 2>/dev/null
        fi
        sync
    done
}

logs_begin_session() {
    # Read straight from the settings file: this runs long before anything that
    # could ask the C side, and the flag has to be on disk to be worth anything
    # after a crash anyway.
    grep -q '^debugLogging=1' "$SHARED_USERDATA_PATH/minuisettings.txt" 2>/dev/null || return 0

    LOGS_ENABLED=1
    LOGS_SESSION_DIR="$LOGS_PATH/session-$(date '+%Y%m%d-%H%M%S' 2>/dev/null || echo unknown)"
    mkdir -p "$LOGS_SESSION_DIR" || { LOGS_ENABLED=0; return 0; }
    LOGS_NEXTUI_LOG="$LOGS_SESSION_DIR/nextui.txt"

    logs_prune

    logs_mark "session started"
    logs_mark "device: ${TRIMUI_MODEL:-unknown} ($DEVICE), platform $PLATFORM"
    if [ -f "$SDCARD_PATH/.system/version.txt" ]; then
        logs_mark "version: $(sed -n '1p' "$SDCARD_PATH/.system/version.txt" | tr -d '\r') / $(sed -n '2p' "$SDCARD_PATH/.system/version.txt" | tr -d '\r')"
    fi
    logs_mark "uptime at start: $(cut -d' ' -f1 /proc/uptime 2>/dev/null)s"

    logs_harvest_previous_boot

    # The boot-time ring buffer, before anything has had a chance to push the
    # kernel's own startup messages out of it.
    dmesg > "$LOGS_SESSION_DIR/02-dmesg-boot.txt" 2>/dev/null

    sync

    logs_heartbeat &
    LOGS_HEARTBEAT_PID=$!
}

logs_end_session() {
    logs_enabled || return 0
    if [ -n "$LOGS_HEARTBEAT_PID" ]; then
        kill "$LOGS_HEARTBEAT_PID" 2>/dev/null
        # Reaped so the shell does not print a job notice over the console on
        # the way to poweroff.
        wait "$LOGS_HEARTBEAT_PID" 2>/dev/null
        LOGS_HEARTBEAT_PID=""
    fi
    logs_mark "session ended cleanly"
    sync
}
