#!/bin/bash

set -e

# ============================================================
# KNST Android Runner
#
# Usage:
#
#   ./run_android.sh
#
#   ./run_android.sh "<device>"
#
#   ./run_android.sh "<device>" "<apk>"
#
# Examples:
#
#   ./run_android.sh
#
#   ./run_android.sh "adb-6bf73a6e-7SZbav (2)._adb-tls-connect._tcp"
#
#   ./run_android.sh "192.168.1.111:41883"
#
# ============================================================



# Configuration


ADB="${ADB:-adb}"

PACKAGE="com.knst.test"
ACTIVITY="android.app.NativeActivity"



# Script / Project Paths


SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

DEFAULT_APK="$PROJECT_ROOT/build-android/knst_app.apk"



# Arguments


DEVICE="${1:-}"

APK="${2:-$DEFAULT_APK}"



# Header


echo ""
echo "========================================"
echo " KNST Android Runner"
echo "========================================"
echo "Device:  $DEVICE"
echo "Package: $PACKAGE"
echo "Activity: $ACTIVITY"
echo "APK:     $APK"
echo ""



# Check ADB


if ! command -v "$ADB" >/dev/null 2>&1; then

    echo "ERROR: adb bulunamadı."
    echo ""
    echo "ADB'nin PATH içerisinde olduğundan emin ol."

    exit 1

fi



if [ -z "$DEVICE" ]; then

    DEVICE=$(
        "$ADB" devices |
        sed -n 's/[[:space:]]\+device[[:space:]]*$//p' |
        head -n 1
    )

fi



# No Device


if [ -z "$DEVICE" ]; then

    echo "ERROR: Bağlı Android cihazı bulunamadı."
    echo ""
    echo "Bağlı cihazlar:"
    echo ""

    "$ADB" devices

    echo ""
    echo "Örnek kullanım:"
    echo ""
    echo "./run_android.sh \"192.168.1.111:41883\""
    echo ""
    echo "./run_android.sh \"adb-xxxx (2)._adb-tls-connect._tcp\""
    echo ""

    exit 1

fi



# Check APK


if [ ! -f "$APK" ]; then

    echo "ERROR: APK bulunamadı:"
    echo "$APK"
    echo ""
    echo "Önce build_android.sh çalıştır."

    exit 1

fi



# Check Current ADB State


DEVICE_LINE=$(
    "$ADB" devices |
    grep -F "$DEVICE" |
    tail -n 1 || true
)



# Device Connection


if echo "$DEVICE_LINE" | grep -q '[[:space:]]device[[:space:]]*$'; then

    echo "Device already connected."

else

    echo "Device is not currently connected."

   
    # IP:PORT device
   

    if [[ "$DEVICE" == *:* ]]; then

        echo ""
        echo "Connecting to:"
        echo "  $DEVICE"
        echo ""

        "$ADB" connect "$DEVICE"

        sleep 1

    else

        echo ""
        echo "ERROR: Cihaz ADB tarafından bağlı görünmüyor:"
        echo "$DEVICE"
        echo ""

        echo "ADB devices:"
        echo ""

        "$ADB" devices

        exit 1

    fi

fi



# Verify Device


DEVICE_LINE=$(
    "$ADB" devices |
    grep -F "$DEVICE" |
    tail -n 1 || true
)


if ! echo "$DEVICE_LINE" | grep -q '[[:space:]]device[[:space:]]*$'; then

    echo ""
    echo "ERROR: ADB cihazı hazır değil."
    echo ""
    echo "Device:"
    echo "$DEVICE"
    echo ""
    echo "ADB devices:"
    echo ""

    "$ADB" devices

    exit 1

fi


echo ""
echo "Device connected successfully."



# Install APK


echo ""
echo "========================================"
echo " Installing APK"
echo "========================================"
echo ""

"$ADB" -s "$DEVICE" install -r "$APK"



# Clear Old Logs


echo ""
echo "Clearing old logcat..."

"$ADB" -s "$DEVICE" logcat -c



# Start Application


echo ""
echo "========================================"
echo " Starting KNST"
echo "========================================"
echo ""

"$ADB" -s "$DEVICE" shell am start \
    -n "$PACKAGE/$ACTIVITY"


echo ""
echo "Application started."
echo ""



# Logcat


echo "========================================"
echo " KNST LOGCAT"
echo "========================================"
echo ""
echo "Press Ctrl+C to stop logcat."
echo ""


"$ADB" -s "$DEVICE" logcat \
    -v color \
    -s KNST:* \
       AndroidRuntime:E \
       DEBUG:E \
       libc:E