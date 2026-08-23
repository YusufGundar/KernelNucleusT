@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: COLOR (ANSI codes for Windows 10+)
:: ============================================================
set "GREEN=[32m"
set "RED=[31m"
set "YELLOW=[33m"
set "CYAN=[36m"
set "NC=[0m"

echo.
echo ========================================
echo   KernelNucleusT - Android Builder
echo ========================================
echo.

:: ============================================================
:: CHECK NDK
:: ============================================================
if "%ANDROID_NDK%"=="" (
    echo %RED%ERROR: ANDROID_NDK is not set!%NC%
    echo Set it with: set ANDROID_NDK=C:\path\to\ndk
    exit /b 1
)

if not exist "%ANDROID_NDK%" (
    echo %RED%ERROR: ANDROID_NDK directory not found: %ANDROID_NDK%%NC%
    exit /b 1
)

echo %GREEN%✓ NDK: %ANDROID_NDK%%NC%

:: ============================================================
:: SELECT GRAPHICS API
:: ============================================================
echo.
echo Select Graphics API:
echo   [1] OpenGL ES
echo   [2] Vulkan
echo   [3] Both (OpenGL + Vulkan)
echo   [4] None (No Graphics)
echo.
set /p GRAPHICS_CHOICE="Enter choice [1-4]: "

if "%GRAPHICS_CHOICE%"=="1" (
    set ENABLE_OPENGL=ON
    set ENABLE_VULKAN=OFF
    set GFX=OpenGL ES
) else if "%GRAPHICS_CHOICE%"=="2" (
    set ENABLE_OPENGL=OFF
    set ENABLE_VULKAN=ON
    set GFX=Vulkan
) else if "%GRAPHICS_CHOICE%"=="3" (
    set ENABLE_OPENGL=ON
    set ENABLE_VULKAN=ON
    set GFX=OpenGL + Vulkan
) else if "%GRAPHICS_CHOICE%"=="4" (
    set ENABLE_OPENGL=OFF
    set ENABLE_VULKAN=OFF
    set GFX=None (No Graphics)
) else (
    echo %RED%Invalid! Using OpenGL%NC%
    set ENABLE_OPENGL=ON
    set ENABLE_VULKAN=OFF
    set GFX=OpenGL ES (default)
)

echo %GREEN%✓ Selected: %GFX%%NC%
echo.

:: ============================================================
:: PROJECT ROOT
:: ============================================================
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "PROJECT_ROOT=%SCRIPT_DIR%\..\..\.."
cd /d "%PROJECT_ROOT%"

echo %CYAN%Project Root:%NC% %PROJECT_ROOT%
echo.

:: ============================================================
:: CLEAN
:: ============================================================
if exist build-android (
    echo Cleaning build-android...
    rmdir /s /q build-android
)

:: ============================================================
:: BUILD
:: ============================================================
echo Building...
echo.

cmake -B build-android ^
    -DCMAKE_TOOLCHAIN_FILE="%ANDROID_NDK%\build\cmake\android.toolchain.cmake" ^
    -DANDROID_ABI=arm64-v8a ^
    -DANDROID_PLATFORM=android-33 ^
    -DANDROID_STL=c++_shared ^
    -DENABLE_OPENGL=%ENABLE_OPENGL% ^
    -DENABLE_VULKAN=%ENABLE_VULKAN% ^
    -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo %RED%❌ CMake configuration failed!%NC%
    exit /b 1
)

echo.
cmake --build build-android --config Release -j %NUMBER_OF_PROCESSORS%

if not exist "build-android\libknst_app.so" (
    echo %RED%❌ Build failed! libknst_app.so not found.%NC%
    exit /b 1
)

echo %GREEN%✓ Build done%NC%
echo.

:: ============================================================
:: APK PACKAGING
:: ============================================================
if "%ANDROID_HOME%"=="" set ANDROID_HOME=%ANDROID_SDK_ROOT%

if "%ANDROID_HOME%"=="" (
    echo %RED%ERROR: ANDROID_HOME / ANDROID_SDK_ROOT is not set.%NC%
    exit /b 1
)

if not exist "%ANDROID_HOME%" (
    echo %RED%ERROR: SDK directory not found: %ANDROID_HOME%%NC%
    exit /b 1
)

:: Find latest build-tools
for /f "usebackq delims=" %%i in (`dir /b /ad /on "%ANDROID_HOME%\build-tools" 2^>nul ^| findstr /r "." ^| sort`) do set "BUILD_TOOLS=%ANDROID_HOME%\build-tools\%%i"

if "%BUILD_TOOLS%"=="" (
    echo %RED%ERROR: No build-tools found in %ANDROID_HOME%\build-tools%NC%
    exit /b 1
)

set "AAPT2=%BUILD_TOOLS%\aapt2.exe"
set "ZIPALIGN=%BUILD_TOOLS%\zipalign.exe"
set "APKSIGNER=%BUILD_TOOLS%\apksigner.exe"
set "ANDROID_JAR=%ANDROID_HOME%\platforms\android-33\android.jar"

echo.
echo ========================================
echo   Android Build Tools
echo ========================================
echo SDK:        %ANDROID_HOME%
echo BuildTools: %BUILD_TOOLS%
echo Framework:  %ANDROID_JAR%
echo.

for %%T in ("%AAPT2%") do if not exist %%T (
    echo %RED%ERROR: Missing tool: aapt2.exe%NC%
    exit /b 1
)
for %%T in ("%ZIPALIGN%") do if not exist %%T (
    echo %RED%ERROR: Missing tool: zipalign.exe%NC%
    exit /b 1
)
for %%T in ("%APKSIGNER%") do if not exist %%T (
    echo %RED%ERROR: Missing tool: apksigner.exe%NC%
    exit /b 1
)

if not exist "%ANDROID_JAR%" (
    echo %RED%ERROR: Missing android-33 platform: %ANDROID_JAR%%NC%
    echo Install with: sdkmanager "platforms;android-33"
    exit /b 1
)

:: ============================================================
:: PREPARE APK
:: ============================================================
cd build-android

if exist apk rmdir /s /q apk
mkdir apk\lib\arm64-v8a

echo Copying native libraries...

copy libknst_app.so apk\lib\arm64-v8a\

:: Copy libc++_shared.so if using shared STL
if exist "%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\aarch64-linux-android\libc++_shared.so" (
    copy "%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\aarch64-linux-android\libc++_shared.so" apk\lib\arm64-v8a\
    echo   - libc++_shared.so copied
)

echo   - libknst_app.so copied
echo.

:: ============================================================
:: CREATE APK
:: ============================================================
echo Creating APK...

"%AAPT2%" link ^
    -I "%ANDROID_JAR%" ^
    --manifest ..\package_tests\knst_window\android\AndroidManifest.xml ^
    --min-sdk-version 33 ^
    --target-sdk-version 33 ^
    --version-code 1 ^
    --version-name "1.0.0" ^
    -o knst_app-unsigned.apk

if not exist "knst_app-unsigned.apk" (
    echo %RED%ERROR: AAPT2 link failed!%NC%
    exit /b 1
)

:: ============================================================
:: ADD LIBRARIES TO APK
:: ============================================================
echo Adding libraries to APK...

cd apk
zip -r ..\knst_app-unsigned.apk lib
cd ..

rmdir /s /q apk

:: ============================================================
:: ZIP ALIGN
:: ============================================================
echo Aligning APK...

"%ZIPALIGN%" -f 4 knst_app-unsigned.apk knst_app-aligned.apk

if not exist "knst_app-aligned.apk" (
    echo %RED%ERROR: ZipAlign failed!%NC%
    exit /b 1
)

:: ============================================================
:: SIGN APK
:: ============================================================
echo Signing APK...

set "KEYSTORE=%USERPROFILE%\.android\debug.keystore"

if not exist "%KEYSTORE%" (
    echo Generating debug keystore...
    mkdir "%USERPROFILE%\.android"
    keytool -genkeypair ^
        -keystore "%KEYSTORE%" ^
        -storepass android ^
        -alias androiddebugkey ^
        -keypass android ^
        -dname "CN=Android Debug,O=Android,C=US" ^
        -keyalg RSA ^
        -keysize 2048 ^
        -validity 10000
)

"%APKSIGNER%" sign ^
    --ks "%KEYSTORE%" ^
    --ks-pass pass:android ^
    --key-pass pass:android ^
    --ks-key-alias androiddebugkey ^
    --out knst_app.apk ^
    knst_app-aligned.apk

if not exist "knst_app.apk" (
    echo %RED%ERROR: APK signing failed!%NC%
    exit /b 1
)

:: ============================================================
:: CLEANUP
:: ============================================================
if exist knst_app-unsigned.apk del knst_app-unsigned.apk
if exist knst_app-aligned.apk del knst_app-aligned.apk

:: ============================================================
:: DONE
:: ============================================================
echo.
echo ========================================
echo %GREEN%✅ APK BUILD SUCCESSFUL!%NC%
echo ========================================
echo.
echo %CYAN%APK:%NC%     %CD%\knst_app.apk
for %%i in (knst_app.apk) do echo %CYAN%Size:%NC%    %%~zi bytes
echo %CYAN%Graphics:%NC% %GFX%
echo.
echo Macros defined:
if "%ENABLE_OPENGL%"=="ON" echo   ✅ KNST_PLATFORM_ANDROID_OPENGL
if "%ENABLE_VULKAN%"=="ON" echo   ✅ KNST_PLATFORM_ANDROID_VULKAN
if "%ENABLE_OPENGL%"=="OFF" if "%ENABLE_VULKAN%"=="OFF" echo   ⚠️  No graphics macros
echo.
echo %CYAN%Install:%NC% adb install knst_app.apk
echo %CYAN%Run:%NC%    adb shell am start -n com.knst.test/android.app.NativeActivity
echo.

endlocal