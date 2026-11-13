#!/usr/bin/env bash
# =============================================================================
#  KernelNucleusT — Android Simple Structures Build, Package & Deploy
#  v8 — MP3 native via minimp3 (no external .so)
# =============================================================================
set -e
set -o pipefail

# ─── Colors ─────────────────────────────────────────────────────────────────
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
SOURCE_FILE="knst_special_anroid_all_simple_test.cpp"
PACKAGE_NAME="com.knst.test.simple"
ACTIVITY_NAME="com.knst.window.KnstActivity"
TARGET_NAME="knst_app"
VERSION_CODE="1"
VERSION_NAME="1.0.0"
PREFER_ADB_PORT="5555"
ENABLE_OPENGL="OFF"
ENABLE_VULKAN="OFF"

# ─── Host OS ────────────────────────────────────────────────────────────────
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

# ─── Paths ──────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SOURCE_PATH="$SCRIPT_DIR/$SOURCE_FILE"
MANIFEST_PATH="$SCRIPT_DIR/AndroidManifest.xml"
BUILD_DIR="$PROJECT_ROOT/build-android-simple"
CACHE_SERIAL="$HOME/.knst_android_serial"
CACHE_WIFI="$HOME/.knst_android_wifi"

if [ -z "$ANDROID_HOME" ]; then
    for candidate in "$HOME/Android/Sdk" "$HOME/Android/sdk" \
                     "$HOME/Library/Android/sdk" "/usr/local/lib/android/sdk" \
                     "$HOME/AppData/Local/Android/Sdk"; do
        [ -d "$candidate" ] && { ANDROID_HOME="$candidate"; break; }
    done
fi
[ -n "$ANDROID_HOME" ] || { err "Android SDK not found. Set ANDROID_HOME."; exit 1; }
export ANDROID_HOME

# ─── Banner ─────────────────────────────────────────────────────────────────
log ""
log "${C}╔═══════════════════════════════════════════════════════════════════╗${N}"
log "${C}║${N}  ${W}KernelNucleusT — Android Simple Structures Tests${N}              ${C}║${N}"
log "${C}╚═══════════════════════════════════════════════════════════════════╝${N}"
log "  ${D}Source :${N} $SOURCE_FILE"
log "  ${D}Package:${N} $PACKAGE_NAME"
log "  ${D}Host OS:${N} $HOST_OS ($HOST_TAG)"
log "  ${D}Scan   :${N} disabled (ML Kit not linked)"
log "  ${D}MP3    :${N} native (minimp3.h — no .so)"
log ""

# ═════════════════════════════════════════════════════════════════════════════
#  [1/8] Environment
# ═════════════════════════════════════════════════════════════════════════════
step "[1/8] Environment"
[ -f "$SOURCE_PATH" ]   || { err "Source file not found: $SOURCE_PATH"; exit 1; }
[ -f "$MANIFEST_PATH" ] || { err "Manifest not found: $MANIFEST_PATH"; exit 1; }

for tool in adb cmake ninja zip keytool unzip awk sed sort javac curl jar timeout; do
    command -v "$tool" > /dev/null 2>&1 || { err "Missing required tool: $tool"; exit 1; }
done
ok "Required tools present"

if [ -z "$ANDROID_NDK" ]; then
    if [ -d "$ANDROID_HOME/ndk" ]; then
        ANDROID_NDK="$(ls -d "$ANDROID_HOME"/ndk/*/ 2>/dev/null | sort -V | tail -1 | sed 's:/*$::')"
    fi
fi
[ -n "$ANDROID_NDK" ] && [ -d "$ANDROID_NDK" ] || { err "NDK not found"; exit 1; }
export ANDROID_NDK
NDK_VERSION="$(basename "$ANDROID_NDK")"

BUILD_TOOLS="$(ls -d "$ANDROID_HOME"/build-tools/*/ 2>/dev/null | sort -V | tail -1 | sed 's:/*$::')"
[ -n "$BUILD_TOOLS" ] || { err "No build-tools"; exit 1; }
BUILD_TOOLS_VERSION="$(basename "$BUILD_TOOLS")"

if [ -z "$KNST_TARGET_SDK" ]; then
    if   [ -f "$ANDROID_HOME/platforms/android-34/android.jar" ]; then KNST_TARGET_SDK=34
    elif [ -f "$ANDROID_HOME/platforms/android-33/android.jar" ]; then KNST_TARGET_SDK=33
    else
        LATEST_PLATFORM="$(ls -d "$ANDROID_HOME"/platforms/android-*/ 2>/dev/null | sort -V | tail -1)"
        [ -n "$LATEST_PLATFORM" ] && KNST_TARGET_SDK="$(basename "$LATEST_PLATFORM" | sed 's/android-//')"
    fi
fi
[ -n "$KNST_TARGET_SDK" ] || { err "No platform installed"; exit 1; }
ANDROID_JAR="$ANDROID_HOME/platforms/android-$KNST_TARGET_SDK/android.jar"
[ -f "$ANDROID_JAR" ] || { err "android.jar missing"; exit 1; }
TARGET_SDK="$KNST_TARGET_SDK"
MIN_SDK="$(grep -oE 'minSdkVersion="[0-9]+"' "$MANIFEST_PATH" 2>/dev/null | head -1 | grep -oE '[0-9]+' || true)"
[ -n "$MIN_SDK" ] || MIN_SDK="24"
ADB_VERSION="$(adb version 2>/dev/null | head -1 | awk '{print $NF}')"
dim "SDK          : $ANDROID_HOME"
dim "NDK          : $NDK_VERSION"
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
    timeout 5 adb -s "$1" get-state 2>/dev/null | grep -q '^device$'
}

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
        [ -z "$ip" ] && ip="$(adb -s "$usb" shell ip -f inet addr show wlan0 2>/dev/null | grep -oE 'inet [0-9.]+' | awk '{print $2}' | head -1)"
        if [ -n "$ip" ]; then echo "${ip}:${PREFER_ADB_PORT}" > "$CACHE_WIFI"; dim "Wi-Fi IP cached: ${ip}:${PREFER_ADB_PORT}"; fi
        echo "$usb"; return 0
    fi

    # 2) WiFi (IP:PORT)
    local wifi
    wifi="$(adb devices | awk 'NR>1 && $2=="device" && $1 ~ /^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+:[0-9]+$/ {print $1; exit}')"
    if [ -n "$wifi" ] && is_ready "$wifi"; then
        ok "WiFi device: $wifi"
        echo "$wifi" > "$CACHE_SERIAL"
        echo "$wifi"; return 0
    fi

    # 3) mDNS
    local mdns
    mdns="$(adb devices | awk 'NR>1 && $2=="device" && $1 ~ /_adb-tls-connect/ {print $1; exit}')"
    if [ -n "$mdns" ] && is_ready "$mdns"; then
        ok "mDNS device: $mdns"
        echo "$mdns" > "$CACHE_SERIAL"
        echo "$mdns"; return 0
    fi

    # 4) Cached serial
    if [ -f "$CACHE_SERIAL" ]; then
        local cached; cached="$(tr -d '\r\n' < "$CACHE_SERIAL")"
        if is_ready "$cached"; then ok "Cached device: $cached"; echo "$cached"; return 0; fi
        warn "Cached serial no longer valid"
    fi

    # 5) Cached WiFi
    if [ -f "$CACHE_WIFI" ]; then
        local wip; wip="$(tr -d '\r\n' < "$CACHE_WIFI")"
        if [ -n "$wip" ]; then
            info "Trying cached Wi-Fi: $wip"
            timeout 5 adb connect "$wip" > /dev/null 2>&1 || true
            sleep 1
            if is_ready "$wip"; then ok "Wi-Fi device: $wip"; echo "$wip" > "$CACHE_SERIAL"; echo "$wip"; return 0; fi
        fi
    fi

    # 6) Wait for any device
    info "Waiting for mDNS discovery (10 s)..."
    local i
    for i in $(seq 1 10); do
        local d; d="$(adb devices | awk 'NR>1 && $2=="device" {print $1; exit}')"
        if [ -n "$d" ]; then printf "\n" >&2; ok "Device appeared: $d"; echo "$d" > "$CACHE_SERIAL"; echo "$d"; return 0; fi
        printf "." >&2; sleep 1
    done
    printf "\n" >&2; return 1
}

ANDROID_SERIAL="$(find_device)" || {
    err "No device found."
    hline
    log "adb devices:"
    adb devices -l 2>&1 | sed 's/^/    /' >&2
    hline
    exit 1
}
export ANDROID_SERIAL

STABLE=0
for i in 1 2 3; do
    if is_ready "$ANDROID_SERIAL"; then STABLE=1; break; fi
    warn "Device temporarily unavailable ($i/3)..."
    sleep 1
done
[ "$STABLE" -eq 1 ] || { err "Device not stable: $ANDROID_SERIAL"; exit 1; }

DEVICE_MODEL="$(timeout 10 adb -s "$ANDROID_SERIAL" shell getprop ro.product.model 2>/dev/null | tr -d '\r' || true)"
DEVICE_ANDROID="$(timeout 10 adb -s "$ANDROID_SERIAL" shell getprop ro.build.version.release 2>/dev/null | tr -d '\r' || true)"
DEVICE_ABI="$(timeout 10 adb -s "$ANDROID_SERIAL" shell getprop ro.product.cpu.abi 2>/dev/null | tr -d '\r' || true)"
DEVICE_SDK="$(timeout 10 adb -s "$ANDROID_SERIAL" shell getprop ro.build.version.sdk 2>/dev/null | tr -d '\r' || true)"
[ -z "$DEVICE_MODEL" ] && DEVICE_MODEL="(unknown)"
[ -z "$DEVICE_ANDROID" ] && DEVICE_ANDROID="?"
[ -z "$DEVICE_SDK" ] && DEVICE_SDK="?"
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
NEED_CONFIGURE=0
if [ ! -f "$BUILD_DIR/build.ninja" ]; then NEED_CONFIGURE=1
elif [ ! -f "$SRC_CACHE" ] || [ "$(cat "$SRC_CACHE")" != "$SOURCE_PATH" ]; then info "Source changed — reconfiguring"; NEED_CONFIGURE=1
elif [ ! -f "$CFG_CACHE" ] || [ "$(cat "$CFG_CACHE")" != "$CURRENT_CFG" ]; then info "Build config changed — reconfiguring"; NEED_CONFIGURE=1
fi
if [ "$NEED_CONFIGURE" -eq 1 ]; then
    rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"
    CMAKE_LOG="$BUILD_DIR/cmake.log"
    if ! cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$MIN_SDK" \
        -DKNST_WINDOW_USE_OPENGL="$ENABLE_OPENGL" -DKNST_WINDOW_USE_VULKAN="$ENABLE_VULKAN" \
        -DKNST_APP_SOURCE="$SOURCE_PATH" > "$CMAKE_LOG" 2>&1; then
        cat "$CMAKE_LOG" >&2; err "CMake configure failed"; exit 1
    fi
    grep -vE '^-- (Detecting|Check|Performing|Found Threads)' "$CMAKE_LOG" || true
    echo "$SOURCE_PATH" > "$SRC_CACHE"; echo "$CURRENT_CFG" > "$CFG_CACHE"
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
T1=$(date +%s); dim "Compile time : $((T1 - T0))s"
LIB_PATH="$BUILD_DIR/lib$TARGET_NAME.so"
[ -f "$LIB_PATH" ] || { err "Shared library missing"; exit 1; }
dim "Library size : $(du -h "$LIB_PATH" | cut -f1)"
ok "Build complete"

# ═════════════════════════════════════════════════════════════════════════════
#  [6/8] APK packaging  (scan disabled — no ML Kit, MP3 native via minimp3.h)
# ═════════════════════════════════════════════════════════════════════════════
step "[6/8] APK packaging"
cd "$BUILD_DIR"
rm -f knst_app*.apk
rm -rf apk && mkdir -p "apk/lib/$ABI"
cp "lib$TARGET_NAME.so" "apk/lib/$ABI/"
dim "OpenSSL: statik (libknst_app.so içinde)"
dim "MP3    : minimp3.h (header-only, exe'ye gömülü)"

# ─── Manifest: eski MlKitInitProvider kayıtlarını temizle ──────────────────
if grep -q "MlKitInitProvider" "$MANIFEST_PATH" 2>/dev/null; then
    info "Removing stale MlKitInitProvider from AndroidManifest.xml"
    sed -i '/MlKitInitProvider/d' "$MANIFEST_PATH"
fi
if grep -q "MlKitComponentDiscoveryService" "$MANIFEST_PATH" 2>/dev/null; then
    info "Removing stale MlKitComponentDiscoveryService from AndroidManifest.xml"
    sed -i '/MlKitComponentDiscoveryService/,+2d' "$MANIFEST_PATH"
fi

# ─── Java → DEX (KnstMlKitScannerHelper.java hariç) ────────────────────────
D8_MIN_API="$DEVICE_SDK"
case "$D8_MIN_API" in
    ''|*[!0-9]*) D8_MIN_API="$MIN_SDK" ;;
esac
dim "d8 --min-api : $D8_MIN_API (device API)"

JAVA_SRC_DIR="$PROJECT_ROOT/include/platform/android"
if [ -d "$JAVA_SRC_DIR" ]; then
    # Scan disabled — always exclude ML Kit Java helper
    JAVA_FILES="$(find "$JAVA_SRC_DIR" -name '*.java' | grep -v 'KnstMlKitScannerHelper.java')"

    if [ -n "$JAVA_FILES" ]; then
        info "Compiling Java sources"
        JAVA_CLASSES="$BUILD_DIR/java-classes"; DEX_DIR="$BUILD_DIR/dex"
        rm -rf "$JAVA_CLASSES" "$DEX_DIR"; mkdir -p "$JAVA_CLASSES" "$DEX_DIR"
        JAVAC_LOG="$BUILD_DIR/javac.log"
        if ! javac -d "$JAVA_CLASSES" -classpath "$ANDROID_JAR" $JAVA_FILES > "$JAVAC_LOG" 2>&1; then
            cat "$JAVAC_LOG" >&2; err "javac failed"; exit 1
        fi
        grep -vE 'warning:|Note:' "$JAVAC_LOG" || true
        D8_INPUTS=$(find "$JAVA_CLASSES" -name '*.class')
        "$BUILD_TOOLS/d8" --output "$DEX_DIR" --lib "$ANDROID_JAR" --min-api "$D8_MIN_API" $D8_INPUTS
        if [ -f "$DEX_DIR/classes.dex" ]; then
            for dex_file in "$DEX_DIR"/classes*.dex; do
                [ -f "$dex_file" ] && cp "$dex_file" "apk/" && dim "$(basename "$dex_file") : $(du -h "$dex_file" | cut -f1)"
            done
        else
            err "d8 failed to produce classes.dex"; exit 1
        fi
    fi
fi

# ─── libc++_shared.so ──────────────────────────────────────────────────────
case "$ABI" in
    arm64-v8a) TRIPLE=aarch64-linux-android;; armeabi-v7a) TRIPLE=arm-linux-androideabi;;
    x86) TRIPLE=i686-linux-android;; x86_64) TRIPLE=x86_64-linux-android;;
esac
TOOLCHAIN_DIR="$ANDROID_NDK/toolchains/llvm/prebuilt/$HOST_TAG"
if [ -d "$TOOLCHAIN_DIR" ]; then
    LIBCXX="$TOOLCHAIN_DIR/sysroot/usr/lib/$TRIPLE/libc++_shared.so"
    if [ -f "$LIBCXX" ]; then cp "$LIBCXX" "apk/lib/$ABI/"; dim "Bundled libc++_shared.so"; fi
fi

# ─── aapt2 link ────────────────────────────────────────────────────────────
"$BUILD_TOOLS/aapt2" link -I "$ANDROID_JAR" \
    --manifest "$MANIFEST_PATH" --rename-manifest-package "$PACKAGE_NAME" \
    --min-sdk-version "$MIN_SDK" --target-sdk-version "$TARGET_SDK" \
    --version-code "$VERSION_CODE" --version-name "$VERSION_NAME" \
    -o knst_app-unsigned.apk

( cd apk && zip -q -r ../knst_app-unsigned.apk . )
rm -rf apk
"$BUILD_TOOLS/zipalign" -f 4 knst_app-unsigned.apk knst_app-aligned.apk

if [ ! -f "$HOME/.android/debug.keystore" ]; then
    info "Generating debug keystore"
    mkdir -p "$HOME/.android"
    keytool -genkeypair -v -keystore "$HOME/.android/debug.keystore" \
        -storepass android -keypass android -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US" > /dev/null 2>&1
fi

"$BUILD_TOOLS/apksigner" sign --ks "$HOME/.android/debug.keystore" \
    --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey \
    --min-sdk-version "$MIN_SDK" --max-sdk-version "$TARGET_SDK" \
    --out knst_app.apk knst_app-aligned.apk
rm -f knst_app-unsigned.apk knst_app-aligned.apk
APK_SIZE="$(du -h knst_app.apk | cut -f1)"
dim "APK size     : $APK_SIZE"
ok "APK created"

# ═════════════════════════════════════════════════════════════════════════════
#  [7/8] Deploy
# ═════════════════════════════════════════════════════════════════════════════
step "[7/8] Deploy"
if ! is_ready "$ANDROID_SERIAL"; then
    warn "Device lost before install — rediscovering..."
    ANDROID_SERIAL="$(find_device)"; export ANDROID_SERIAL
    is_ready "$ANDROID_SERIAL" || { err "Cannot re-acquire device"; exit 1; }
fi
info "Uninstalling previous version"
adb -s "$ANDROID_SERIAL" uninstall "$PACKAGE_NAME" > /dev/null 2>&1 || true
info "Installing APK (runtime permissions auto-granted)"
T2=$(date +%s); adb -s "$ANDROID_SERIAL" install --no-incremental -r -t -g knst_app.apk; T3=$(date +%s)
dim "Install time : $((T3 - T2))s"
info "Granting special permissions (best-effort)"
adb -s "$ANDROID_SERIAL" shell appops set "$PACKAGE_NAME" MANAGE_EXTERNAL_STORAGE allow > /dev/null 2>&1 || true
adb -s "$ANDROID_SERIAL" logcat -c
info "Launching activity"
if adb -s "$ANDROID_SERIAL" shell am start -n "$PACKAGE_NAME/$ACTIVITY_NAME" > /dev/null 2>&1; then ok "Application launched"
else err "Failed to launch activity"; fi

# ═════════════════════════════════════════════════════════════════════════════
#  [8/8] Live logcat
# ═════════════════════════════════════════════════════════════════════════════
log ""
hline
log "  ${G}✓ BUILD · PACKAGE · DEPLOY · LAUNCH — COMPLETE${N}"
hline
log "  ${D}Device        :${N} $DEVICE_MODEL (Android $DEVICE_ANDROID / API $DEVICE_SDK)"
log "  ${D}Serial        :${N} $ANDROID_SERIAL"
log "  ${D}Source        :${N} $SOURCE_FILE"
log "  ${D}ABI           :${N} $ABI"
log "  ${D}Package       :${N} $PACKAGE_NAME"
log "  ${D}SDK / NDK     :${N} $TARGET_SDK / $NDK_VERSION"
log "  ${D}d8 min-api    :${N} $D8_MIN_API"
log "  ${D}Scan          :${N} disabled (no ML Kit)"
log "  ${D}MP3           :${N} native (minimp3.h — no .so)"
log "  ${D}APK           :${N} build-android-simple/knst_app.apk ($APK_SIZE)"
log "  ${D}Total elapsed :${N} ${SECONDS}s"
log ""
log "  ${Y}Live logcat starting — press Ctrl+C to stop${N}"
hline
log ""
exec adb -s "$ANDROID_SERIAL" logcat -v color KNST:V AndroidRuntime:E libc:E DEBUG:E *:S