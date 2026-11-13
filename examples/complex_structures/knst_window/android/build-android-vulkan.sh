#!/usr/bin/env bash
# =============================================================================
#  KernelNucleusT — Android (Vulkan) Build, Package & Deploy
# =============================================================================
#
#  Targets Vulkan 1.x. Has its own build dir and package name so it can
#  live side-by-side with the OpenGL build on the same device.
#
#  Vulkan needs minSdkVersion >= 24 (Android 7.0). The script bails out
#  early if the manifest declares anything lower.
#
#  Pipeline:
#    [1/8] Environment       — SDK, NDK, tools, host OS
#    [2/8] ADB server        — start or reuse existing connection
#    [3/8] Device discovery  — USB / mDNS / cache / Wi-Fi / wait
#    [4/8] CMake configure   — skipped when nothing changed
#    [5/8] Native build      — parallel ninja
#    [6/8] APK packaging     — aapt2 + Java/DEX + zipalign + apksigner
#    [7/8] Deploy            — uninstall → install → launch
#    [8/8] Live logcat       — filtered stream (Ctrl+C to stop)
#
# =============================================================================
set -e
set -o pipefail


# ─── Colors and logging helpers ─────────────────────────────────────────────
if [ -t 1 ]; then
    R='\033[0;31m'; G='\033[0;32m'; Y='\033[1;33m'; B='\033[0;34m'
    M='\033[0;35m'; C='\033[0;36m'; W='\033[1;37m'; D='\033[0;90m'; N='\033[0m'
else
    R=''; G=''; Y=''; B=''; M=''; C=''; W=''; D=''; N=''
fi

log()   { printf "%b\n" "$*" >&2; }
info()  { log "${B}[INFO]${N}  $*"; }
ok()    { log "${G}[ OK ]${N}  $*"; }
warn()  { log "${Y}[WARN]${N}  $*"; }
err()   { log "${R}[ERR ]${N}  $*"; }
step()  { log "\n${M}▶${N} ${W}$*${N}"; }
dim()   { log "${D}        $*${N}"; }
hline() { log "${D}────────────────────────────────────────────────────────────${N}"; }


# ═════════════════════════════════════════════════════════════════════════════
#  Configuration
# ═════════════════════════════════════════════════════════════════════════════
SOURCE_FILE="knst_window_android_vulkan_test.cpp"
PACKAGE_NAME="com.knst.test.vk"
ACTIVITY_NAME="com.knst.window.KnstActivity"
TARGET_NAME="knst_app"
VERSION_CODE="1"
VERSION_NAME="1.0.0"
PREFER_ADB_PORT="5555"

# Backend: Vulkan
ENABLE_OPENGL="OFF"
ENABLE_VULKAN="ON"


# ═════════════════════════════════════════════════════════════════════════════
#  Host OS detection
# ═════════════════════════════════════════════════════════════════════════════
UNAME_S="$(uname -s)"
case "$UNAME_S" in
    Linux*)               HOST_OS="linux";;
    Darwin*)              HOST_OS="darwin";;
    MINGW*|MSYS*|CYGWIN*) HOST_OS="windows";;
    *)                    HOST_OS="linux";;
esac

HOST_TAG=""
case "$HOST_OS" in
    linux)   HOST_TAG="linux-x86_64";;
    darwin)  HOST_TAG="darwin-x86_64";;
    windows) HOST_TAG="windows-x86_64";;
esac


# ═════════════════════════════════════════════════════════════════════════════
#  Paths
# ═════════════════════════════════════════════════════════════════════════════
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
SOURCE_PATH="$SCRIPT_DIR/$SOURCE_FILE"
MANIFEST_PATH="$SCRIPT_DIR/AndroidManifest.xml"
BUILD_DIR="$PROJECT_ROOT/build-android-vulkan"
CACHE_SERIAL="$HOME/.knst_android_serial"
CACHE_WIFI="$HOME/.knst_android_wifi"

# Find the SDK if not already exported
if [ -z "$ANDROID_HOME" ]; then
    for candidate in \
        "$HOME/Android/Sdk" \
        "$HOME/Android/sdk" \
        "$HOME/Library/Android/sdk" \
        "/usr/local/lib/android/sdk" \
        "$HOME/AppData/Local/Android/Sdk"
    do
        [ -d "$candidate" ] && { ANDROID_HOME="$candidate"; break; }
    done
fi
[ -n "$ANDROID_HOME" ] || { err "Android SDK not found. Set ANDROID_HOME."; exit 1; }
export ANDROID_HOME


# ═════════════════════════════════════════════════════════════════════════════
#  Banner
# ═════════════════════════════════════════════════════════════════════════════
log ""
log "${C}╔═══════════════════════════════════════════════════════════════════╗${N}"
log "${C}║${N}  ${W}KernelNucleusT — Android Build & Deploy (Vulkan)${N}               ${C}║${N}"
log "${C}╚═══════════════════════════════════════════════════════════════════╝${N}"
log "  ${D}Source :${N} $SOURCE_FILE"
log "  ${D}Package:${N} $PACKAGE_NAME"
log "  ${D}Host OS:${N} $HOST_OS ($HOST_TAG)"
log "  ${D}Backend:${N} Vulkan 1.x"
log ""


# ═════════════════════════════════════════════════════════════════════════════
#  [1/8] Environment
# ═════════════════════════════════════════════════════════════════════════════
step "[1/8] Environment"

[ -f "$SOURCE_PATH" ]   || { err "Source file not found: $SOURCE_PATH"; exit 1; }
[ -f "$MANIFEST_PATH" ] || { err "Manifest not found: $MANIFEST_PATH"; exit 1; }

# javac is here because we compile the IME bridge on the fly
for tool in adb cmake ninja zip keytool unzip awk sed sort javac; do
    command -v "$tool" > /dev/null 2>&1 || { err "Missing required tool: $tool"; exit 1; }
done
ok "Required tools present"

[ -d "$ANDROID_HOME" ] || { err "ANDROID_HOME not found: $ANDROID_HOME"; exit 1; }

# Pick the newest NDK if not set
if [ -z "$ANDROID_NDK" ]; then
    if [ -d "$ANDROID_HOME/ndk" ]; then
        ANDROID_NDK="$(ls -d "$ANDROID_HOME"/ndk/*/ 2>/dev/null \
                        | sort -V | tail -1 | sed 's:/*$::')"
    fi
fi
if [ -z "$ANDROID_NDK" ] || [ ! -d "$ANDROID_NDK" ]; then
    err "No Android NDK found under $ANDROID_HOME/ndk"
    [ -d "$ANDROID_HOME/ndk" ] && ls "$ANDROID_HOME/ndk/" | sed 's/^/    /' >&2
    exit 1
fi
export ANDROID_NDK
NDK_VERSION="$(basename "$ANDROID_NDK")"

# Newest build-tools
BUILD_TOOLS="$(ls -d "$ANDROID_HOME"/build-tools/*/ 2>/dev/null \
                | sort -V | tail -1 | sed 's:/*$::')"
[ -n "$BUILD_TOOLS" ] || { err "No build-tools under $ANDROID_HOME/build-tools"; exit 1; }
BUILD_TOOLS_VERSION="$(basename "$BUILD_TOOLS")"

# Target SDK — prefer 34, then 33, then whatever's installed
if [ -z "$KNST_TARGET_SDK" ]; then
    if   [ -f "$ANDROID_HOME/platforms/android-34/android.jar" ]; then
        KNST_TARGET_SDK=34
    elif [ -f "$ANDROID_HOME/platforms/android-33/android.jar" ]; then
        KNST_TARGET_SDK=33
    else
        LATEST_PLATFORM="$(ls -d "$ANDROID_HOME"/platforms/android-*/ 2>/dev/null \
                           | sort -V | tail -1)"
        if [ -n "$LATEST_PLATFORM" ]; then
            KNST_TARGET_SDK="$(basename "$LATEST_PLATFORM" | sed 's/android-//')"
        fi
    fi
fi
[ -n "$KNST_TARGET_SDK" ] || { err "No Android platform installed under $ANDROID_HOME/platforms"; exit 1; }

ANDROID_JAR="$ANDROID_HOME/platforms/android-$KNST_TARGET_SDK/android.jar"
[ -f "$ANDROID_JAR" ] || { err "android.jar missing for API $KNST_TARGET_SDK"; exit 1; }

TARGET_SDK="$KNST_TARGET_SDK"
MIN_SDK="$(grep -oE 'minSdkVersion="[0-9]+"' "$MANIFEST_PATH" 2>/dev/null \
           | head -1 | grep -oE '[0-9]+' || true)"
[ -n "$MIN_SDK" ] || MIN_SDK="24"

# Vulkan needs API 24 or newer
if [ "$MIN_SDK" -lt 24 ] 2>/dev/null; then
    err "Vulkan requires minSdkVersion >= 24.  Current: $MIN_SDK"
    exit 1
fi

ADB_VERSION="$(adb version 2>/dev/null | head -1 | awk '{print $NF}')"

dim "SDK          : $ANDROID_HOME"
dim "NDK          : $NDK_VERSION  ($ANDROID_NDK)"
dim "Build tools  : $BUILD_TOOLS_VERSION"
dim "Target SDK   : $TARGET_SDK  (min $MIN_SDK)"
dim "ADB version  : $ADB_VERSION"
ok "Environment ready"


# ═════════════════════════════════════════════════════════════════════════════
#  [2/8] ADB server
# ═════════════════════════════════════════════════════════════════════════════
step "[2/8] ADB server"

EXISTING_DEV="$(adb devices 2>/dev/null | awk 'NR>1 && $2=="device" {print $1; exit}')"
if [ -n "$EXISTING_DEV" ]; then
    ok "ADB already running — device attached: $EXISTING_DEV"
else
    info "No device attached; restarting ADB with mDNS discovery enabled"
    adb kill-server > /dev/null 2>&1 || true
    ADB_MDNS_OPENSCREEN=1 adb start-server > /dev/null 2>&1 || true
    sleep 2
fi

# Drop stale offline entries so they don't confuse discovery
for dev in $(adb devices | awk 'NR>1 && $2=="offline" {print $1}'); do
    warn "Disconnecting offline device: $dev"
    adb disconnect "$dev" > /dev/null 2>&1 || true
done


# ═════════════════════════════════════════════════════════════════════════════
#  [3/8] Device discovery
# ═════════════════════════════════════════════════════════════════════════════
step "[3/8] Device discovery"

is_ready() {
    [ -n "$1" ] || return 1
    adb -s "$1" get-state 2>/dev/null | grep -q '^device$'
}

# Try USB first, then mDNS, then cached serial, then cached Wi-Fi, then wait.
find_device() {
    # 1) USB
    local usb
    usb="$(adb devices | awk 'NR>1 && $2=="device" && $1 !~ /:/ && $1 !~ /_adb-tls-connect/ {print $1; exit}')"
    if [ -n "$usb" ]; then
        info "USB device: $usb"
        adb -s "$usb" tcpip "$PREFER_ADB_PORT" > /dev/null 2>&1 || true
        sleep 2
        local ip
        ip="$(adb -s "$usb" shell ip route 2>/dev/null | grep -oE 'src [0-9.]+' | awk '{print $2}' | head -1)"
        [ -z "$ip" ] && ip="$(adb -s "$usb" shell ip -f inet addr show wlan0 2>/dev/null \
                               | grep -oE 'inet [0-9.]+' | awk '{print $2}' | head -1)"
        if [ -n "$ip" ]; then
            echo "${ip}:${PREFER_ADB_PORT}" > "$CACHE_WIFI"
            dim "Wi-Fi IP cached: ${ip}:${PREFER_ADB_PORT}"
        fi
        echo "$usb"; return 0
    fi

    # 2) mDNS
    local mdns
    mdns="$(adb devices | awk 'NR>1 && $2=="device" && $1 ~ /_adb-tls-connect/ {print $1; exit}')"
    if [ -n "$mdns" ] && is_ready "$mdns"; then
        ok "mDNS device: $mdns"
        echo "$mdns" > "$CACHE_SERIAL"
        echo "$mdns"; return 0
    fi

    # 3) Cached serial
    if [ -f "$CACHE_SERIAL" ]; then
        local cached
        cached="$(tr -d '\r\n' < "$CACHE_SERIAL")"
        if is_ready "$cached"; then
            ok "Cached device: $cached"
            echo "$cached"; return 0
        fi
        warn "Cached serial no longer valid"
    fi

    # 4) Cached Wi-Fi
    if [ -f "$CACHE_WIFI" ]; then
        local wip
        wip="$(tr -d '\r\n' < "$CACHE_WIFI")"
        if [ -n "$wip" ]; then
            info "Trying cached Wi-Fi: $wip"
            adb connect "$wip" > /dev/null 2>&1 || true
            sleep 1
            if is_ready "$wip"; then
                ok "Wi-Fi device: $wip"
                echo "$wip" > "$CACHE_SERIAL"
                echo "$wip"; return 0
            fi
        fi
    fi

    # 5) Wait for mDNS
    info "Waiting for mDNS discovery (10 s)..."
    local i
    for i in $(seq 1 10); do
        local d
        d="$(adb devices | awk 'NR>1 && $2=="device" {print $1; exit}')"
        if [ -n "$d" ]; then
            printf "\n" >&2
            ok "Device appeared: $d"
            echo "$d" > "$CACHE_SERIAL"
            echo "$d"; return 0
        fi
        printf "." >&2
        sleep 1
    done
    printf "\n" >&2
    return 1
}

# No device — show help and exit
ANDROID_SERIAL="$(find_device)" || {
    err "No device found."
    log ""
    hline
    log "  ${Y}How to connect a device${N}"
    hline
    log "  ${W}Option 1 — USB cable${N} (recommended first time)"
    log "    • Plug the phone in"
    log "    • Enable Developer Options → USB Debugging"
    log "    • Re-run this script"
    log ""
    log "  ${W}Option 2 — Wireless (VSCode)${N}"
    log "    • Install: ${C}Android ADB WLAN — Wireless Debugging${N}"
    log "    • Pair + connect from the extension UI"
    log "    • Re-run this script"
    log ""
    log "  ${W}Option 3 — Wireless (manual)${N}"
    log "    • Phone → Developer Options → Wireless Debugging"
    log "    • Read the IP:PORT shown on screen"
    log "    • Run: ${C}adb pair IP:PAIR_PORT${N}   then"
    log "           ${C}adb connect IP:PORT${N}"
    log ""
    hline
    log "  Current adb devices:"
    adb devices -l 2>&1 | sed 's/^/    /' >&2 || true
    hline
    exit 1
}
export ANDROID_SERIAL

# Make sure the device is stable before we trust it
STABLE=0
for i in 1 2 3; do
    if is_ready "$ANDROID_SERIAL"; then STABLE=1; break; fi
    warn "Device temporarily unavailable ($i/3)..."
    sleep 1
done
[ "$STABLE" -eq 1 ] || { err "Device not stable: $ANDROID_SERIAL"; exit 1; }

# Device info
DEVICE_MODEL="$(adb -s "$ANDROID_SERIAL" shell getprop ro.product.model 2>/dev/null | tr -d '\r')"
DEVICE_ANDROID="$(adb -s "$ANDROID_SERIAL" shell getprop ro.build.version.release 2>/dev/null | tr -d '\r')"
DEVICE_ABI="$(adb -s "$ANDROID_SERIAL" shell getprop ro.product.cpu.abi 2>/dev/null | tr -d '\r')"
DEVICE_SDK="$(adb -s "$ANDROID_SERIAL" shell getprop ro.build.version.sdk 2>/dev/null | tr -d '\r')"
[ -z "$DEVICE_MODEL" ]   && DEVICE_MODEL="(unknown)"
[ -z "$DEVICE_ANDROID" ] && DEVICE_ANDROID="?"
[ -z "$DEVICE_SDK" ]     && DEVICE_SDK="?"

# Match the device ABI unless user overrides
ABI="${KNST_ABI:-$DEVICE_ABI}"
case "$ABI" in
    arm64-v8a|armeabi-v7a|x86|x86_64) ;;
    *) warn "Unknown ABI '$ABI' — defaulting to arm64-v8a"; ABI="arm64-v8a";;
esac

dim "Model        : $DEVICE_MODEL"
dim "Android      : $DEVICE_ANDROID (API $DEVICE_SDK)"
dim "Device ABI   : $DEVICE_ABI"
dim "Build ABI    : $ABI"
dim "Serial       : $ANDROID_SERIAL"
ok "Device ready"


# ═════════════════════════════════════════════════════════════════════════════
#  [4/8] CMake configure
# ═════════════════════════════════════════════════════════════════════════════
step "[4/8] CMake configure"

SRC_CACHE="$BUILD_DIR/.knst_last_source"
CFG_CACHE="$BUILD_DIR/.knst_last_config"
CURRENT_CFG="$ABI|$MIN_SDK|gl=$ENABLE_OPENGL|vk=$ENABLE_VULKAN|$SOURCE_PATH"

# Skip configure if nothing important changed
NEED_CONFIGURE=0
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    NEED_CONFIGURE=1
elif [ ! -f "$SRC_CACHE" ] || [ "$(cat "$SRC_CACHE")" != "$SOURCE_PATH" ]; then
    info "Source changed — reconfiguring"
    NEED_CONFIGURE=1
elif [ ! -f "$CFG_CACHE" ] || [ "$(cat "$CFG_CACHE")" != "$CURRENT_CFG" ]; then
    info "Build config changed — reconfiguring"
    NEED_CONFIGURE=1
fi

if [ "$NEED_CONFIGURE" -eq 1 ]; then
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"

    CMAKE_LOG="$BUILD_DIR/cmake.log"

    # Log to a file first. Piping straight into grep + pipefail is
    # fragile: if grep finds nothing it returns 1 and kills the script.
    if ! cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM="android-$MIN_SDK" \
        -DKNST_WINDOW_USE_OPENGL="$ENABLE_OPENGL" \
        -DKNST_WINDOW_USE_VULKAN="$ENABLE_VULKAN" \
        -DKNST_APP_SOURCE="$SOURCE_PATH" > "$CMAKE_LOG" 2>&1; then

        cat "$CMAKE_LOG" >&2
        err "CMake configure failed"
        exit 1
    fi

    # Show everything except the noisy detection lines
    grep -vE '^-- (Detecting|Check|Performing|Found Threads)' "$CMAKE_LOG" || true

    echo "$SOURCE_PATH"  > "$SRC_CACHE"
    echo "$CURRENT_CFG"  > "$CFG_CACHE"
    ok "Configured"
else
    ok "Build directory up-to-date"
fi


# ═════════════════════════════════════════════════════════════════════════════
#  [5/8] Native build
# ═════════════════════════════════════════════════════════════════════════════
step "[5/8] Native build"

T0=$(date +%s)
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
T1=$(date +%s)
dim "Compile time : $((T1 - T0))s"

LIB_PATH="$BUILD_DIR/lib$TARGET_NAME.so"
[ -f "$LIB_PATH" ] || { err "Shared library missing: lib$TARGET_NAME.so"; exit 1; }
dim "Library size : $(du -h "$LIB_PATH" | cut -f1)"
ok "Build complete"


# ═════════════════════════════════════════════════════════════════════════════
#  [6/8] APK packaging
# ═════════════════════════════════════════════════════════════════════════════
step "[6/8] APK packaging"

cd "$BUILD_DIR"

rm -f knst_app*.apk
rm -rf apk && mkdir -p "apk/lib/$ABI"
cp "lib$TARGET_NAME.so" "apk/lib/$ABI/"

# ─── Java → DEX (IME / InputConnection bridge) ──────────────────────────────
#
# Java source lives in the C++ header folder:
#     include/platform/android/KnstActivity.java
# We only pick up *.java, so the .hpp files won't get mixed in.
#
JAVA_SRC_DIR="$PROJECT_ROOT/include/platform/android"
if [ -d "$JAVA_SRC_DIR" ]; then
    JAVA_FILES="$(find "$JAVA_SRC_DIR" -name '*.java')"
    if [ -n "$JAVA_FILES" ]; then
        info "Compiling Java sources (IME bridge)"

        JAVA_CLASSES="$BUILD_DIR/java-classes"
        DEX_DIR="$BUILD_DIR/dex"
        rm -rf "$JAVA_CLASSES" "$DEX_DIR"
        mkdir -p "$JAVA_CLASSES" "$DEX_DIR"

        JAVAC_LOG="$BUILD_DIR/javac.log"

        # Same pipefail story as cmake above — log to file first.
        if ! javac -d "$JAVA_CLASSES" \
                   -classpath "$ANDROID_JAR" \
                   $JAVA_FILES > "$JAVAC_LOG" 2>&1; then

            cat "$JAVAC_LOG" >&2
            err "javac failed"
            exit 1
        fi

        grep -vE 'warning:|Note:' "$JAVAC_LOG" || true

        # Convert .class files into a single DEX
        "$BUILD_TOOLS/d8" \
            --output "$DEX_DIR" \
            --lib "$ANDROID_JAR" \
            --min-api "$MIN_SDK" \
            $(find "$JAVA_CLASSES" -name '*.class')

        if [ -f "$DEX_DIR/classes.dex" ]; then
            cp "$DEX_DIR/classes.dex" "apk/classes.dex"
            dim "classes.dex : $(du -h apk/classes.dex | cut -f1)"
        else
            err "d8 failed to produce classes.dex"
            exit 1
        fi
    fi
fi

# Bundle libc++_shared.so for the target ABI
case "$ABI" in
    arm64-v8a)   TRIPLE=aarch64-linux-android;;
    armeabi-v7a) TRIPLE=arm-linux-androideabi;;
    x86)         TRIPLE=i686-linux-android;;
    x86_64)      TRIPLE=x86_64-linux-android;;
esac

TOOLCHAIN_DIR="$ANDROID_NDK/toolchains/llvm/prebuilt/$HOST_TAG"
if [ -d "$TOOLCHAIN_DIR" ]; then
    LIBCXX="$TOOLCHAIN_DIR/sysroot/usr/lib/$TRIPLE/libc++_shared.so"
    if [ -f "$LIBCXX" ]; then
        cp "$LIBCXX" "apk/lib/$ABI/"
        dim "Bundled libc++_shared.so (from $TRIPLE)"
    fi
fi

# Link resources + manifest → unsigned APK
"$BUILD_TOOLS/aapt2" link -I "$ANDROID_JAR" \
    --manifest "$MANIFEST_PATH" \
    --rename-manifest-package "$PACKAGE_NAME" \
    --min-sdk-version "$MIN_SDK" \
    --target-sdk-version "$TARGET_SDK" \
    --version-code "$VERSION_CODE" \
    --version-name "$VERSION_NAME" \
    -o knst_app-unsigned.apk

# Pack everything under apk/ — libs + classes.dex
( cd apk && zip -q -r ../knst_app-unsigned.apk . )
rm -rf apk

"$BUILD_TOOLS/zipalign" -f 4 knst_app-unsigned.apk knst_app-aligned.apk

# Create a debug keystore once, reuse it forever after
if [ ! -f "$HOME/.android/debug.keystore" ]; then
    info "Generating debug keystore"
    mkdir -p "$HOME/.android"
    keytool -genkeypair -v \
        -keystore "$HOME/.android/debug.keystore" \
        -storepass android -keypass android \
        -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US" > /dev/null 2>&1
fi

"$BUILD_TOOLS/apksigner" sign \
    --ks "$HOME/.android/debug.keystore" \
    --ks-pass pass:android --key-pass pass:android \
    --ks-key-alias androiddebugkey \
    --out knst_app.apk knst_app-aligned.apk

rm -f knst_app-unsigned.apk knst_app-aligned.apk

APK_SIZE="$(du -h knst_app.apk | cut -f1)"
dim "APK size     : $APK_SIZE"
ok "APK created"


# ═════════════════════════════════════════════════════════════════════════════
#  [7/8] Deploy
# ═════════════════════════════════════════════════════════════════════════════
step "[7/8] Deploy"

# Device may have dropped between build and install — retry discovery
if ! is_ready "$ANDROID_SERIAL"; then
    warn "Device lost before install — rediscovering..."
    ANDROID_SERIAL="$(find_device)"
    export ANDROID_SERIAL
    is_ready "$ANDROID_SERIAL" || { err "Cannot re-acquire device"; exit 1; }
fi

info "Uninstalling previous version"
adb -s "$ANDROID_SERIAL" uninstall "$PACKAGE_NAME" > /dev/null 2>&1 || true

info "Installing APK"
T2=$(date +%s)
adb -s "$ANDROID_SERIAL" install -r knst_app.apk
T3=$(date +%s)
dim "Install time : $((T3 - T2))s"

adb -s "$ANDROID_SERIAL" logcat -c
info "Launching activity"
if adb -s "$ANDROID_SERIAL" shell am start -n "$PACKAGE_NAME/$ACTIVITY_NAME" > /dev/null 2>&1; then
    ok "Application launched"
else
    err "Failed to launch activity"
fi


# ═════════════════════════════════════════════════════════════════════════════
#  [8/8] Live logcat
# ═════════════════════════════════════════════════════════════════════════════
log ""
hline
log "  ${G}✓ BUILD · PACKAGE · DEPLOY · LAUNCH — COMPLETE (Vulkan)${N}"
hline
log "  ${D}Device        :${N} $DEVICE_MODEL (Android $DEVICE_ANDROID / API $DEVICE_SDK)"
log "  ${D}Serial        :${N} $ANDROID_SERIAL"
log "  ${D}Source        :${N} $SOURCE_FILE"
log "  ${D}ABI           :${N} $ABI"
log "  ${D}Backend       :${N} Vulkan"
log "  ${D}Package       :${N} $PACKAGE_NAME"
log "  ${D}SDK / NDK     :${N} $TARGET_SDK / $NDK_VERSION"
log "  ${D}APK           :${N} build-android-vulkan/knst_app.apk ($APK_SIZE)"
log "  ${D}Total elapsed :${N} ${SECONDS}s"
log ""
log "  ${Y}Live logcat starting — press Ctrl+C to stop${N}"
hline
log ""

exec adb -s "$ANDROID_SERIAL" logcat -v color \
    KNST:V \
    AndroidRuntime:E \
    libc:E \
    DEBUG:E \
    *:S