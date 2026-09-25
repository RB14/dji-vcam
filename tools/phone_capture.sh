#!/usr/bin/env bash
# Records a DJI Mimo <-> camera session on a rooted Android phone over adb:
#   - Wi-Fi: tcpdump on all interfaces (Mimo may use wlan1 for the camera while wlan0 stays home)
#   - BLE:   the Bluetooth HCI snoop log (enabled in "full" mode by `prepare`)
#
# Usage: phone_capture.sh prepare|start|stop|pull [serial]
#   prepare  enable the full HCI snoop log and restart Bluetooth
#   start    begin tcpdump in the background (survives adb disconnects)
#   stop     stop tcpdump
#   pull     copy the pcap and btsnoop log into captures/<timestamp>/
set -euo pipefail

ADB="${ADB:-adb.exe}"  # Windows adb on the PATH, or set ADB to its path
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CAPTURE_ROOT="$SCRIPT_DIR/../captures"
REMOTE_PCAP=/data/local/tmp/mimo.pcap
REMOTE_BTSNOOP=/data/misc/bluetooth/logs/btsnoop_hci.log

action="${1:-}"
serial="${2:-}"
adb() { if [ -n "$serial" ]; then "$ADB" -s "$serial" "$@"; else "$ADB" "$@"; fi; }
su_shell() { adb shell "su -c '$1'" | tr -d '\r'; }

case "$action" in
    prepare)
        su_shell "setprop persist.bluetooth.btsnooplogmode full"
        adb shell settings put secure bluetooth_hci_log 1
        adb shell cmd bluetooth_manager disable >/dev/null; sleep 3
        adb shell cmd bluetooth_manager enable >/dev/null; sleep 3
        su_shell "ls -la $REMOTE_BTSNOOP"
        ;;
    start)
        su_shell "rm -f $REMOTE_PCAP; nohup tcpdump -i any -s 0 -U -w $REMOTE_PCAP >/dev/null 2>&1 &"
        sleep 1
        su_shell "pidof tcpdump" | sed 's/^/tcpdump pid: /'
        ;;
    stop)
        su_shell "pkill -INT tcpdump; sleep 1; ls -la $REMOTE_PCAP"
        ;;
    pull)
        dest="$CAPTURE_ROOT/$(date +%Y%m%d-%H%M%S)"
        mkdir -p "$dest"
        su_shell "cp $REMOTE_PCAP /sdcard/mimo.pcap; cp $REMOTE_BTSNOOP /sdcard/btsnoop_hci.log; chmod 644 /sdcard/mimo.pcap /sdcard/btsnoop_hci.log"
        adb pull /sdcard/mimo.pcap "$(wslpath -w "$dest")\\mimo.pcap" >/dev/null
        adb pull /sdcard/btsnoop_hci.log "$(wslpath -w "$dest")\\btsnoop_hci.log" >/dev/null
        ls -la "$dest"
        ;;
    *)
        sed -n '2,11p' "$0"
        exit 1
        ;;
esac
