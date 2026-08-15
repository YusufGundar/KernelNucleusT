#!/bin/bash

set -e

# ============================================================
# COLOR
# ============================================================
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo ""
echo "========================================"
echo "  KernelNucleusT - Android Builder"
echo "========================================"
echo ""

# ============================================================
# CHECK NDK
# ============================================================
if [ -z "$ANDROID_NDK" ]; then
    echo -e "${RED}ERROR: ANDROID_NDK is not set!${NC}"
    echo "Set it with: export ANDROID_NDK=/path/to/ndk"
    exit 1
fi

if [ ! -d "$ANDROID_NDK" ]; then
    echo -e "${RED}ERROR: ANDROID_NDK directory not found: $ANDROID_NDK${NC}"
    exit 1
fi

echo -e "${GREEN}✓ NDK: $ANDROID_NDK${NC}"

# ============================================================
# SELECT GRAPHICS API
# ============================================================
echo ""
echo "Select Graphics API:"
echo "  [1] OpenGL ES"
echo "  [2] Vulkan"
echo "  [3] Both (OpenGL + Vulkan)"
echo "  [4] None (No Graphics)"
echo ""
read -p "Enter choice [1-4]: " GRAPHICS_CHOICE

case $GRAPHICS_CHOICE in
    1) ENABLE_OPENGL="ON"; ENABLE_VULKAN="OFF"; GFX="OpenGL ES" ;;
    2) ENABLE_OPENGL="OFF"; ENABLE_VULKAN="ON"; GFX="Vulkan" ;;
    3) ENABLE_OPENGL="ON"; ENABLE_VULKAN="ON"; GFX="OpenGL + Vulkan" ;;
    4) ENABLE_OPENGL="OFF"; ENABLE_VULKAN="OFF"; GFX="None (No Graphics)" ;;
    *) echo -e "${RED}Invalid! Using OpenGL${NC}"; ENABLE_OPENGL="ON"; ENABLE_VULKAN="OFF"; GFX="OpenGL ES (default)" ;;
esac

echo -e "${GREEN}✓ Selected: $GFX${NC}"
echo ""

# ============================================================
# PROJECT ROOT
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
cd "$PROJECT_ROOT"

echo -e "${CYAN}Project Root:${NC} $PROJECT_ROOT"
echo ""

# ============================================================
# CLEAN
# ============================================================
rm -rf build-android

# ============================================================
# BUILD
# ============================================================
echo "Building..."
echo ""

cmake -B build-android \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-33 \
    -DANDROID_STL=c++_shared \
    -DENABLE_OPENGL="$ENABLE_OPENGL" \
    -DENABLE_VULKAN="$ENABLE_VULKAN" \
    -DCMAKE_BUILD_TYPE=Release

echo ""
cmake --build build-android -j$(nproc)

if [ ! -f "build-android/libknst_app.so" ]; then
    echo -e "${RED}❌ Build failed! libknst_app.so not found.${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Build done${NC}"
echo ""

# ============================================================
# APK PACKAGING
# ============================================================
SDK_ROOT="${ANDROID_HOME:-${ANDROID_SDK_ROOT}}"
if [ -z "$SDK_ROOT" ]; then
    echo -e "${RED}ERROR: ANDROID_HOME / ANDROID_SDK_ROOT is not set.${NC}"
    exit 1
fi

if [ ! -d "$SDK_ROOT" ]; then
    echo -e "${RED}ERROR: SDK directory not found: $SDK_ROOT${NC}"
    exit 1
fi

BUILD_TOOLS=$(find "$SDK_ROOT/build-tools" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n 1)

if [ -z "$BUILD_TOOLS" ]; then
    echo -e "${RED}ERROR: No build-tools found in $SDK_ROOT/build-tools${NC}"
    exit 1
fi

AAPT2="$BUILD_TOOLS/aapt2"
ZIPALIGN="$BUILD_TOOLS/zipalign"
APKSIGNER="$BUILD_TOOLS/apksigner"
ANDROID_JAR="$SDK_ROOT/platforms/android-33/android.jar"

echo ""
echo "========================================"
echo "  Android Build Tools"
echo "========================================"
echo "SDK:        $SDK_ROOT"
echo "BuildTools: $BUILD_TOOLS"
echo "Framework:  $ANDROID_JAR"
echo ""

for TOOL in "$AAPT2" "$ZIPALIGN" "$APKSIGNER"; do
    if [ ! -f "$TOOL" ]; then
        echo -e "${RED}ERROR: Missing tool: $(basename $TOOL)${NC}"
        exit 1
    fi
done

if [ ! -f "$ANDROID_JAR" ]; then
    echo -e "${RED}ERROR: Missing android-33 platform: $ANDROID_JAR${NC}"
    echo "Install with: sdkmanager \"platforms;android-33\""
    exit 1
fi

# ============================================================
# PREPARE APK
# ============================================================
cd build-android

rm -rf apk
mkdir -p apk/lib/arm64-v8a

echo "Copying native libraries..."

cp libknst_app.so apk/lib/arm64-v8a/

# Copy libc++_shared.so if using shared STL
if [ -f "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" ]; then
    cp "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" apk/lib/arm64-v8a/
    echo "  - libc++_shared.so copied"
fi

echo "  - libknst_app.so copied"
echo ""

# ============================================================
# CREATE APK
# ============================================================
echo "Creating APK..."

"$AAPT2" link \
    -I "$ANDROID_JAR" \
    --manifest ../package_tests/knst_window/android/AndroidManifest.xml \
    --min-sdk-version 33 \
    --target-sdk-version 33 \
    --version-code 1 \
    --version-name "1.0.0" \
    -o knst_app-unsigned.apk

if [ ! -f "knst_app-unsigned.apk" ]; then
    echo -e "${RED}ERROR: AAPT2 link failed!${NC}"
    exit 1
fi

# ============================================================
# ADD LIBRARIES TO APK
# ============================================================
echo "Adding libraries to APK..."

cd apk
zip -r ../knst_app-unsigned.apk lib
cd ..

rm -rf apk

# ============================================================
# ZIP ALIGN
# ============================================================
echo "Aligning APK..."

"$ZIPALIGN" -f 4 knst_app-unsigned.apk knst_app-aligned.apk

if [ ! -f "knst_app-aligned.apk" ]; then
    echo -e "${RED}ERROR: ZipAlign failed!${NC}"
    exit 1
fi

# ============================================================
# SIGN APK
# ============================================================
echo "Signing APK..."

KEYSTORE="$HOME/.android/debug.keystore"

if [ ! -f "$KEYSTORE" ]; then
    echo "Generating debug keystore..."
    mkdir -p "$HOME/.android"
    keytool -genkeypair \
        -keystore "$KEYSTORE" \
        -storepass android \
        -alias androiddebugkey \
        -keypass android \
        -dname "CN=Android Debug,O=Android,C=US" \
        -keyalg RSA \
        -keysize 2048 \
        -validity 10000
fi

"$APKSIGNER" sign \
    --ks "$KEYSTORE" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --ks-key-alias androiddebugkey \
    --out knst_app.apk \
    knst_app-aligned.apk

if [ ! -f "knst_app.apk" ]; then
    echo -e "${RED}ERROR: APK signing failed!${NC}"
    exit 1
fi

# ============================================================
# CLEANUP
# ============================================================
rm -f knst_app-unsigned.apk knst_app-aligned.apk

# ============================================================
# DONE
# ============================================================
echo ""
echo "========================================"
echo -e "${GREEN}✅ APK BUILD SUCCESSFUL!${NC}"
echo "========================================"
echo ""
echo -e "${CYAN}APK:${NC}     $(pwd)/knst_app.apk"
echo -e "${CYAN}Size:${NC}    $(du -h knst_app.apk | cut -f1)"
echo -e "${CYAN}Graphics:${NC} $GFX"
echo ""
echo "Macros defined:"
if [ "$ENABLE_OPENGL" = "ON" ]; then
    echo "  ✅ KNST_PLATFORM_ANDROID_OPENGL"
fi
if [ "$ENABLE_VULKAN" = "ON" ]; then
    echo "  ✅ KNST_PLATFORM_ANDROID_VULKAN"
fi
if [ "$ENABLE_OPENGL" = "OFF" ] && [ "$ENABLE_VULKAN" = "OFF" ]; then
    echo "  ⚠️  No graphics macros"
fi
echo ""
echo -e "${CYAN}Install:${NC} adb install knst_app.apk"
echo -e "${CYAN}Run:${NC}    adb shell am start -n com.knst.test/android.app.NativeActivity"
echo ""