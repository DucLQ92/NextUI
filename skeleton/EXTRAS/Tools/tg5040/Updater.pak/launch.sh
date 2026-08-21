#!/bin/sh
# Updater.pak — over-the-air update for NextUI.
#
# This only *stages* an update: it downloads MinUI.zip to the root of the SD card
# and reboots. Installing it is the job of .tmp_update/<platform>.sh, the same
# path used when you copy MinUI.zip across by hand, so nothing here unpacks or
# overwrites a live system.

cd "$(dirname "$0")" || exit 1

# Overridable so the flow can be exercised against a local server without
# publishing a release.
RELEASE_BASE="${RELEASE_BASE:-https://github.com/DucLQ92/NextUI/releases/latest/download}"
SDCARD_PATH="${SDCARD_PATH:-/mnt/SDCARD}"
LOGO="$SDCARD_PATH/.system/res/logo.png"
LOCAL_VERSION="$SDCARD_PATH/.system/version.txt"

# Staged under a temp name and only renamed into place once the download has been
# verified — the boot installer treats any MinUI.zip it finds as a real update, so
# a half-finished download must never carry that name.
TMP_ZIP="$SDCARD_PATH/.MinUI.zip.part"
FINAL_ZIP="$SDCARD_PATH/MinUI.zip"

# show2 splits --text on real newline bytes, so a backslash-n written in a shell
# string reaches it literally and gets drawn on screen as the two characters
# instead of breaking the line.
NL='
'

# -k because the device has no CA bundle -- http.c passes it for the same reason
# and every HTTPS request fails certificate verification without it.
# --connect-timeout / --speed-* so a half-open connection can't stall forever.
CURL_OPTS="-k --connect-timeout 10 --speed-limit 1024 --speed-time 30"

FIFO="/tmp/show2.fifo"
SHOW_PID=""
UI_FD_OPEN=0

ui_start() {
    show2.elf --mode=daemon --image="$LOGO" --text="$1" &
    SHOW_PID=$!
    # wait for show2 to create the fifo before writing to it
    i=0
    while [ ! -p "$FIFO" ] && [ $i -lt 50 ]; do
        usleep 100000 2>/dev/null || sleep 1
        i=$((i + 1))
    done
    # Hold the fifo open read-write for the rest of the run. Opening a fifo
    # O_RDWR returns immediately, and keeping a reader of our own means a write
    # can never block -- whereas plain `echo > fifo` blocks until something
    # opens the read end, so if show2 died the updater would hang forever with
    # a frozen splash and no way out but pulling the power.
    if [ -p "$FIFO" ]; then
        exec 3<> "$FIFO"
        UI_FD_OPEN=1
    fi
}
ui_text()     { [ "$UI_FD_OPEN" = 1 ] && echo "TEXT:$1" >&3; }
ui_progress() { [ "$UI_FD_OPEN" = 1 ] && echo "PROGRESS:$1" >&3; }
ui_stop() {
    if [ "$UI_FD_OPEN" = 1 ]; then
        echo "QUIT" >&3
        exec 3>&-
        UI_FD_OPEN=0
    fi
    if [ -n "$SHOW_PID" ]; then
        # Give it a couple of seconds to act on QUIT, then stop waiting on it.
        # `wait` alone would hang if show2 ever ignored the command.
        i=0
        while kill -0 "$SHOW_PID" 2>/dev/null && [ $i -lt 20 ]; do
            usleep 100000 2>/dev/null || sleep 1
            i=$((i + 1))
        done
        kill "$SHOW_PID" 2>/dev/null
        wait "$SHOW_PID" 2>/dev/null
        SHOW_PID=""
    fi
}

# Leave nothing behind if we exit early, are killed, or fail partway.
cleanup() {
    rm -f "$TMP_ZIP"
    ui_stop
}
trap cleanup EXIT INT TERM

fail() {
    ui_stop
    show2.elf --mode=simple --image="$LOGO" --text="$1" --timeout=4
    exit 1
}

ui_start "Đang kiểm tra cập nhật..."

# curl comes from the stock firmware, not from anything we ship -- the same
# dependency http.c already relies on for RetroAchievements. Checked separately
# so a missing binary doesn't get reported as a network failure.
if ! command -v curl >/dev/null 2>&1; then
    fail "Không tìm thấy curl trên máy.${NL}Không thể tự cập nhật."
fi

# --- connectivity -----------------------------------------------------------
# Checked by looking for an IPv4 address on the interface, not by reaching out to
# a host. curl's -m cannot interrupt a blocking getaddrinfo(), so with Wi-Fi off
# the name lookup for github.com can hang well past any timeout -- and a pak has
# no way to be cancelled, so the device is stuck on the splash until it is force
# restarted. Reading the interface state costs nothing and never touches DNS.
if ! ip -4 addr show wlan0 2>/dev/null | grep -q "inet "; then
    fail "Chưa kết nối Wi-Fi.${NL}Hãy bật Wi-Fi rồi thử lại."
fi

# --- what is installed ------------------------------------------------------
# version.txt is two lines: release name, then the short build hash. The hash is
# what we compare on: release names collide across rebuilds of the same day.
if [ -f "$LOCAL_VERSION" ]; then
    LOCAL_NAME=$(sed -n '1p' "$LOCAL_VERSION" | tr -d '\r')
    LOCAL_HASH=$(sed -n '2p' "$LOCAL_VERSION" | tr -d '\r')
else
    LOCAL_NAME=""
    LOCAL_HASH=""
fi

# --- what is published ------------------------------------------------------
REMOTE_VERSION=$(curl -sfL $CURL_OPTS -m 20 "$RELEASE_BASE/version.txt" 2>/dev/null)
if [ -z "$REMOTE_VERSION" ]; then
    fail "Không đọc được thông tin phiên bản.${NL}Có thể chưa có bản phát hành nào."
fi
REMOTE_NAME=$(printf '%s\n' "$REMOTE_VERSION" | sed -n '1p' | tr -d '\r')
REMOTE_HASH=$(printf '%s\n' "$REMOTE_VERSION" | sed -n '2p' | tr -d '\r')

if [ -n "$LOCAL_HASH" ] && [ "$LOCAL_HASH" = "$REMOTE_HASH" ]; then
    ui_stop
    show2.elf --mode=simple --image="$LOGO" \
        --text="Bạn đang dùng bản mới nhất.${NL}$LOCAL_NAME" --timeout=4
    exit 0
fi

# --- battery ----------------------------------------------------------------
# The install runs on the next boot and rewrites .system; losing power in the
# middle of that is how cards get bricked.
CAPACITY=$(cat /sys/class/power_supply/axp2202-battery/capacity 2>/dev/null)
CHARGING=$(cat /sys/class/power_supply/axp2202-battery/status 2>/dev/null)
if [ -n "$CAPACITY" ] && [ "$CAPACITY" -lt 30 ] && [ "$CHARGING" != "Charging" ]; then
    fail "Pin còn $CAPACITY%.${NL}Hãy sạc trên 30% rồi cập nhật."
fi

# --- download ---------------------------------------------------------------
ui_text "Đang tải $REMOTE_NAME..."
ui_progress 0

rm -f "$TMP_ZIP"
# --fail so an HTML error page is never mistaken for a payload.
curl -sfL $CURL_OPTS -m 900 -o "$TMP_ZIP" "$RELEASE_BASE/MinUI.zip" &
CURL_PID=$!

# Drive the bar off the bytes on disk rather than parsing curl's own meter,
# which needs a tty. The payload size changes from build to build, so ask for it
# rather than assuming; the fallback only affects how honest the bar looks.
EXPECTED=$(curl -sfLI $CURL_OPTS -m 20 "$RELEASE_BASE/MinUI.zip" 2>/dev/null \
    | tr -d '\r' | awk 'tolower($1) == "content-length:" { print $2 }' | tail -1)
case "$EXPECTED" in
    ''|*[!0-9]*) EXPECTED=20000000 ;;
esac
[ "$EXPECTED" -le 0 ] 2>/dev/null && EXPECTED=20000000

while kill -0 "$CURL_PID" 2>/dev/null; do
    if [ -f "$TMP_ZIP" ]; then
        # busybox stat understands -c; fall back to wc so a stat that doesn't
        # can't leave the bar frozen at zero for the whole download.
        SIZE=$(stat -c %s "$TMP_ZIP" 2>/dev/null) || SIZE=""
        case "$SIZE" in
            ''|*[!0-9]*) SIZE=$(wc -c < "$TMP_ZIP" 2>/dev/null | tr -d ' ') ;;
        esac
        case "$SIZE" in
            ''|*[!0-9]*) SIZE=0 ;;
        esac
        PCT=$((SIZE * 100 / EXPECTED))
        [ "$PCT" -gt 99 ] && PCT=99
        ui_progress "$PCT"
    fi
    sleep 1
done
wait "$CURL_PID"
CURL_RC=$?

[ "$CURL_RC" -ne 0 ] && fail "Tải thất bại.${NL}Kiểm tra kết nối rồi thử lại."
[ ! -s "$TMP_ZIP" ] && fail "Tệp tải về rỗng.${NL}Hãy thử lại."

# Cheap integrity check without depending on unzip being on PATH: every zip
# starts with "PK\x03\x04".
MAGIC=$(dd if="$TMP_ZIP" bs=1 count=2 2>/dev/null)
if [ "$MAGIC" != "PK" ]; then
    fail "Tệp tải về không hợp lệ.${NL}Hãy thử lại."
fi

ui_progress 100
ui_text "Đang hoàn tất..."

# Only now does it get the name the boot installer looks for.
mv -f "$TMP_ZIP" "$FINAL_ZIP" || fail "Không ghi được vào thẻ nhớ."
sync

ui_stop
show2.elf --mode=simple --image="$LOGO" \
    --text="Đã tải xong $REMOTE_NAME.${NL}Máy sẽ khởi động lại để cài đặt." --timeout=5

# The staged zip is the update now; don't let the exit trap delete it.
trap - EXIT INT TERM

reboot_next 2>/dev/null || reboot
