@echo off
REM =============================================================================
REM  KernelNucleusT - Android Simple Structures Build, Package & Deploy
REM  v8 (Windows) - MP3 native via minimp3 (no external .so)
REM =============================================================================
setlocal EnableDelayedExpansion EnableExtensions
chcp 65001 >nul 2>&1

REM Enable ANSI colors on Windows 10 1511+
reg add "HKCU\Console" /v VirtualTerminalLevel /t REG_DWORD /d 1 /f >nul 2>&1

REM Get ESC char
for /f "tokens=2" %%a in ('echo prompt $E^| cmd') do set "ESC=%%a"

REM ---- Colors ---------------------------------------------------------------
set "R=!ESC![91m"
set "G=!ESC![92m"
set "Y=!ESC![93m"
set "B=!ESC![94m"
set "M=!ESC![95m"
set "C=!ESC![96m"
set "W=!ESC![97m"
set "D=!ESC![90m"
set "N=!ESC![0m"

REM =============================================================================
REM  Configuration
REM =============================================================================
set "SOURCE_FILE=knst_special_anroid_all_simple_test.cpp"
set "PACKAGE_NAME=com.knst.test.simple"
set "ACTIVITY_NAME=com.knst.window.KnstActivity"
set "TARGET_NAME=knst_app"
set "VERSION_CODE=1"
set "VERSION_NAME=1.0.0"
set "PREFER_ADB_PORT=5555"
set "ENABLE_OPENGL=OFF"
set "ENABLE_VULKAN=OFF"

REM ---- Host OS (always windows for .bat) ------------------------------------
set "HOST_OS=windows"
set "HOST_TAG=windows-x86_64"

REM ---- Paths ----------------------------------------------------------------
set "SCRIPT_DIR=%~dp0"
if "!SCRIPT_DIR:~-1!"=="\" set "SCRIPT_DIR=!SCRIPT_DIR:~0,-1!"

for %%i in ("!SCRIPT_DIR!\..\..\..") do set "PROJECT_ROOT=%%~fi"

set "SOURCE_PATH=!SCRIPT_DIR!\!SOURCE_FILE!"
set "MANIFEST_PATH=!SCRIPT_DIR!\AndroidManifest.xml"
set "BUILD_DIR=!PROJECT_ROOT!\build-android-simple"
set "CACHE_SERIAL=%USERPROFILE%\.knst_android_serial"
set "CACHE_WIFI=%USERPROFILE%\.knst_android_wifi"

REM ---- Find Android SDK -----------------------------------------------------
if not defined ANDROID_HOME (
    if exist "%USERPROFILE%\Android\Sdk"                   set "ANDROID_HOME=%USERPROFILE%\Android\Sdk"
    if not defined ANDROID_HOME if exist "%USERPROFILE%\Android\sdk"          set "ANDROID_HOME=%USERPROFILE%\Android\sdk"
    if not defined ANDROID_HOME if exist "%USERPROFILE%\AppData\Local\Android\Sdk" set "ANDROID_HOME=%USERPROFILE%\AppData\Local\Android\Sdk"
    if not defined ANDROID_HOME if exist "C:\Android\Sdk"                    set "ANDROID_HOME=C:\Android\Sdk"
)
if not defined ANDROID_HOME (
    echo !R![ERR ]!N! Android SDK not found. Set ANDROID_HOME.
    exit /b 1
)

REM ---- Banner ---------------------------------------------------------------
echo.
echo !C!+=======================================================+!N!
echo !C!^|!N!  !W!KernelNucleusT - Android Simple Structures Tests!N!  !C!^|!N!
echo !C!+=======================================================+!N!
echo   !D!Source :!N! !SOURCE_FILE!
echo   !D!Package:!N! !PACKAGE_NAME!
echo   !D!Host OS:!N! !HOST_OS! (!HOST_TAG!)
echo   !D!Scan   :!N! disabled (ML Kit not linked)
echo   !D!MP3    :!N! native (minimp3.h - no .so)
echo.

REM =============================================================================
REM  [1/8] Environment
REM =============================================================================
echo !M!^>!N! !W![1/8] Environment!N!

if not exist "!SOURCE_PATH!" (
    echo !R![ERR ]!N! Source file not found: !SOURCE_PATH!
    exit /b 1
)
if not exist "!MANIFEST_PATH!" (
    echo !R![ERR ]!N! Manifest not found: !MANIFEST_PATH!
    exit /b 1
)

for %%t in (adb cmake ninja zip keytool unzip javac curl jar) do (
    where %%t >nul 2>&1
    if errorlevel 1 (
        echo !R![ERR ]!N! Missing required tool: %%t
        exit /b 1
    )
)
echo !G![ OK ]!N!  Required tools present

REM ---- Find NDK -------------------------------------------------------------
if not defined ANDROID_NDK (
    if exist "!ANDROID_HOME!\ndk" (
        for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "(Get-ChildItem '!ANDROID_HOME!\ndk' -Directory -ErrorAction SilentlyContinue ^| Sort-Object { [version]($_.Name -replace '[^0-9.]','') } ^| Select-Object -Last 1).FullName"`) do (
            set "ANDROID_NDK=%%i"
        )
    )
)
if not defined ANDROID_NDK (
    echo !R![ERR ]!N! NDK not found. Set ANDROID_NDK.
    exit /b 1
)
for %%i in ("!ANDROID_NDK!") do set "NDK_VERSION=%%~nxi"

REM ---- Find build-tools -----------------------------------------------------
set "BUILD_TOOLS="
set "BUILD_TOOLS_VERSION="
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "(Get-ChildItem '!ANDROID_HOME!\build-tools' -Directory -ErrorAction SilentlyContinue ^| Sort-Object { [version]($_.Name -replace '[^0-9.]','') } ^| Select-Object -Last 1).FullName"`) do (
    set "BUILD_TOOLS=%%i"
)
if not defined BUILD_TOOLS (
    echo !R![ERR ]!N! No build-tools
    exit /b 1
)
for %%i in ("!BUILD_TOOLS!") do set "BUILD_TOOLS_VERSION=%%~nxi"

REM ---- Find target SDK ------------------------------------------------------
if not defined KNST_TARGET_SDK (
    if exist "!ANDROID_HOME!\platforms\android-34\android.jar" (
        set "KNST_TARGET_SDK=34"
    ) else if exist "!ANDROID_HOME!\platforms\android-33\android.jar" (
        set "KNST_TARGET_SDK=33"
    ) else (
        for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "(Get-ChildItem '!ANDROID_HOME!\platforms' -Directory -ErrorAction SilentlyContinue ^| Where-Object { $_.Name -match '^android-\d+$' } ^| Sort-Object { [int]($_.Name -replace 'android-','') } ^| Select-Object -Last 1).Name -replace 'android-',''"`) do (
            set "KNST_TARGET_SDK=%%i"
        )
    )
)
if not defined KNST_TARGET_SDK (
    echo !R![ERR ]!N! No platform installed
    exit /b 1
)
set "ANDROID_JAR=!ANDROID_HOME!\platforms\android-!KNST_TARGET_SDK!\android.jar"
if not exist "!ANDROID_JAR!" (
    echo !R![ERR ]!N! android.jar missing: !ANDROID_JAR!
    exit /b 1
)
set "TARGET_SDK=!KNST_TARGET_SDK!"

REM ---- Parse min SDK --------------------------------------------------------
set "MIN_SDK=24"
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "if ((Get-Content '!MANIFEST_PATH!' -Raw -ErrorAction SilentlyContinue) -match 'minSdkVersion=.(\d+)') { $matches[1] }"`) do set "MIN_SDK=%%i"

echo   !D!SDK          :!N! !ANDROID_HOME!
echo   !D!NDK          :!N! !NDK_VERSION!
echo   !D!Build tools  :!N! !BUILD_TOOLS_VERSION!
echo   !D!Target SDK   :!N! !TARGET_SDK!  (min !MIN_SDK!)
echo !G![ OK ]!N!  Environment ready

REM =============================================================================
REM  [2/8] ADB server
REM =============================================================================
echo.
echo !M!^>!N! !W![2/8] ADB server!N!

set "EXISTING_DEV="
for /f "skip=1 tokens=1,2" %%a in ('adb devices 2^>nul') do (
    if "%%b"=="device" if not defined EXISTING_DEV set "EXISTING_DEV=%%a"
)
if defined EXISTING_DEV (
    echo !G![ OK ]!N!  ADB already running - device attached: !EXISTING_DEV!
) else (
    echo !B![INFO]!N!  No device attached; restarting ADB with mDNS discovery enabled
    adb kill-server >nul 2>&1
    set "ADB_MDNS_OPENSCREEN=1"
    adb start-server >nul 2>&1
    set "ADB_MDNS_OPENSCREEN="
    timeout /t 2 /nobreak >nul
)
for /f "skip=1 tokens=1,2" %%a in ('adb devices 2^>nul') do (
    if "%%b"=="offline" (
        echo !Y![WARN]!N!  Disconnecting offline device: %%a
        adb disconnect %%a >nul 2>&1
    )
)

REM =============================================================================
REM  [3/8] Device discovery
REM =============================================================================
echo.
echo !M!^>!N! !W![3/8] Device discovery!N!

call :find_device
if not defined ANDROID_SERIAL (
    echo !R![ERR ]!N! No device found.
    echo !D!-----------------------------------------------------------!N!
    echo adb devices:
    adb devices -l 2>&1
    echo !D!-----------------------------------------------------------!N!
    exit /b 1
)

REM Stability check
set "STABLE=0"
for %%i in (1 2 3) do (
    if "!STABLE!"=="0" (
        call :is_ready "!ANDROID_SERIAL!"
        if !errorlevel!==0 (
            set "STABLE=1"
        ) else (
            echo !Y![WARN]!N!  Device temporarily unavailable (%%i/3)...
            timeout /t 1 /nobreak >nul
        )
    )
)
if "!STABLE!"=="0" (
    echo !R![ERR ]!N! Device not stable: !ANDROID_SERIAL!
    exit /b 1
)

set "DEVICE_MODEL="
set "DEVICE_ANDROID="
set "DEVICE_ABI="
set "DEVICE_SDK="
for /f "delims=" %%i in ('adb -s "!ANDROID_SERIAL!" shell getprop ro.product.model 2^>nul') do if not defined DEVICE_MODEL set "DEVICE_MODEL=%%i"
for /f "delims=" %%i in ('adb -s "!ANDROID_SERIAL!" shell getprop ro.build.version.release 2^>nul') do if not defined DEVICE_ANDROID set "DEVICE_ANDROID=%%i"
for /f "delims=" %%i in ('adb -s "!ANDROID_SERIAL!" shell getprop ro.product.cpu.abi 2^>nul') do if not defined DEVICE_ABI set "DEVICE_ABI=%%i"
for /f "delims=" %%i in ('adb -s "!ANDROID_SERIAL!" shell getprop ro.build.version.sdk 2^>nul') do if not defined DEVICE_SDK set "DEVICE_SDK=%%i"

if not defined DEVICE_MODEL   set "DEVICE_MODEL=(unknown)"
if not defined DEVICE_ANDROID set "DEVICE_ANDROID=?"
if not defined DEVICE_SDK     set "DEVICE_SDK=?"

if defined KNST_ABI (
    set "ABI=!KNST_ABI!"
) else (
    set "ABI=!DEVICE_ABI!"
)
if "!ABI!"=="" set "ABI=arm64-v8a"
if not "!ABI!"=="arm64-v8a" if not "!ABI!"=="armeabi-v7a" if not "!ABI!"=="x86" if not "!ABI!"=="x86_64" (
    echo !Y![WARN]!N!  Unknown ABI '!ABI!' - defaulting to arm64-v8a
    set "ABI=arm64-v8a"
)

echo   !D!Model        :!N! !DEVICE_MODEL!
echo   !D!Android      :!N! !DEVICE_ANDROID! (API !DEVICE_SDK!)
echo   !D!Device ABI   :!N! !DEVICE_ABI!
echo   !D!Build ABI    :!N! !ABI!
echo   !D!Serial       :!N! !ANDROID_SERIAL!
echo !G![ OK ]!N!  Device ready

REM =============================================================================
REM  [4/8] CMake configure
REM =============================================================================
echo.
echo !M!^>!N! !W![4/8] CMake configure!N!

set "SRC_CACHE=!BUILD_DIR!\.knst_last_source"
set "CFG_CACHE=!BUILD_DIR!\.knst_last_config"
set "CURRENT_CFG=!ABI!^|!MIN_SDK!^|gl=!ENABLE_OPENGL!^|vk=!ENABLE_VULKAN!^|!SOURCE_PATH!"

set "NEED_CONFIGURE=0"
if not exist "!BUILD_DIR!\build.ninja" set "NEED_CONFIGURE=1"
if "!NEED_CONFIGURE!"=="0" (
    set "LAST_SOURCE="
    set "LAST_CFG="
    if exist "!SRC_CACHE!" for /f "usebackq delims=" %%i in ("!SRC_CACHE!") do set "LAST_SOURCE=%%i"
    if exist "!CFG_CACHE!" for /f "usebackq delims=" %%i in ("!CFG_CACHE!") do set "LAST_CFG=%%i"
    if not "!LAST_SOURCE!"=="!SOURCE_PATH!" (
        echo !B![INFO]!N!  Source changed - reconfiguring
        set "NEED_CONFIGURE=1"
    ) else if not "!LAST_CFG!"=="!CURRENT_CFG!" (
        echo !B![INFO]!N!  Build config changed - reconfiguring
        set "NEED_CONFIGURE=1"
    )
)

if "!NEED_CONFIGURE!"=="1" (
    if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"
    mkdir "!BUILD_DIR!"
    set "CMAKE_LOG=!BUILD_DIR!\cmake.log"
    cmake -S "!PROJECT_ROOT!" -B "!BUILD_DIR!" -G Ninja ^
        -DCMAKE_TOOLCHAIN_FILE="!ANDROID_NDK!\build\cmake\android.toolchain.cmake" ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DANDROID_ABI="!ABI!" ^
        -DANDROID_PLATFORM="android-!MIN_SDK!" ^
        -DKNST_WINDOW_USE_OPENGL="!ENABLE_OPENGL!" ^
        -DKNST_WINDOW_USE_VULKAN="!ENABLE_VULKAN!" ^
        -DKNST_APP_SOURCE="!SOURCE_PATH!" > "!CMAKE_LOG!" 2>&1
    if !errorlevel! neq 0 (
        type "!CMAKE_LOG!"
        echo !R![ERR ]!N! CMake configure failed
        exit /b 1
    )
    type "!CMAKE_LOG!"
    >"!SRC_CACHE!" echo !SOURCE_PATH!
    >"!CFG_CACHE!" echo !CURRENT_CFG!
    echo !G![ OK ]!N!  Configured
) else (
    echo !G![ OK ]!N!  Build directory up-to-date
)

REM =============================================================================
REM  [5/8] Native build
REM =============================================================================
echo.
echo !M!^>!N! !W![5/8] Native build!N!

call :get_unix_time T0
cmake --build "!BUILD_DIR!"
if !errorlevel! neq 0 (
    echo !R![ERR ]!N! Build failed
    exit /b 1
)
call :get_unix_time T1
set /a COMPILE_TIME=!T1! - !T0!
echo   !D!Compile time : !COMPILE_TIME!s!N!

set "LIB_PATH=!BUILD_DIR!\lib!TARGET_NAME!.so"
if not exist "!LIB_PATH!" (
    echo !R![ERR ]!N! Shared library missing: !LIB_PATH!
    exit /b 1
)
for %%i in ("!LIB_PATH!") do set "LIB_SIZE=%%~zi"
set /a LIB_SIZE_KB=!LIB_SIZE! / 1024
echo   !D!Library size : !LIB_SIZE_KB! KB!N!
echo !G![ OK ]!N!  Build complete

REM =============================================================================
REM  [6/8] APK packaging
REM =============================================================================
echo.
echo !M!^>!N! !W![6/8] APK packaging!N!

pushd "!BUILD_DIR!"
if exist knst_app*.apk del /q knst_app*.apk >nul 2>&1
if exist apk rmdir /s /q apk
mkdir "apk\lib\!ABI!"
copy /y "lib!TARGET_NAME!.so" "apk\lib\!ABI!\" >nul
echo   !D!OpenSSL: static (inside lib!TARGET_NAME!.so)!N!
echo   !D!MP3    : minimp3.h (header-only, embedded in exe)!N!

REM ---- Clean stale ML Kit manifest entries ---------------------------------
findstr /c:"MlKitInitProvider" "!MANIFEST_PATH!" >nul 2>&1
if !errorlevel! equ 0 (
    echo !B![INFO]!N!  Removing stale MlKitInitProvider from AndroidManifest.xml
    powershell -NoProfile -Command "(Get-Content '!MANIFEST_PATH!') ^| Where-Object { $_ -notmatch 'MlKitInitProvider' } ^| Set-Content '!MANIFEST_PATH!'"
)
findstr /c:"MlKitComponentDiscoveryService" "!MANIFEST_PATH!" >nul 2>&1
if !errorlevel! equ 0 (
    echo !B![INFO]!N!  Removing stale MlKitComponentDiscoveryService from AndroidManifest.xml
    powershell -NoProfile -Command "$L = Get-Content '!MANIFEST_PATH!'; $O = @(); for ($i=0; $i -lt $L.Count; $i++) { if ($L[$i] -match 'MlKitComponentDiscoveryService') { $i += 2; continue }; $O += $L[$i] }; $O ^| Set-Content '!MANIFEST_PATH!'"
)

REM ---- Java -> DEX --------------------------------------------------------
set "D8_MIN_API=!DEVICE_SDK!"
echo !D8_MIN_API!| findstr /r "^[0-9][0-9]*$" >nul
if !errorlevel! neq 0 set "D8_MIN_API=!MIN_SDK!"
echo   !D!d8 --min-api : !D8_MIN_API! (device API)!N!

set "JAVA_SRC_DIR=!PROJECT_ROOT!\include\platform\android"
if exist "!JAVA_SRC_DIR!" (
    set "JAVA_LIST=!BUILD_DIR!\java_sources.txt"
    type nul > "!JAVA_LIST!"
    for /f "delims=" %%i in ('dir /b /s "!JAVA_SRC_DIR!\*.java" 2^>nul') do (
        echo %%i | findstr /i /c:"KnstMlKitScannerHelper.java" >nul
        if !errorlevel! neq 0 echo %%i>> "!JAVA_LIST!"
    )
    for %%A in ("!JAVA_LIST!") do set "JAVA_LIST_SIZE=%%~zA"

    if !JAVA_LIST_SIZE! gtr 0 (
        echo !B![INFO]!N!  Compiling Java sources
        set "JAVA_CLASSES=!BUILD_DIR!\java-classes"
        set "DEX_DIR=!BUILD_DIR!\dex"
        if exist "!JAVA_CLASSES!" rmdir /s /q "!JAVA_CLASSES!"
        if exist "!DEX_DIR!" rmdir /s /q "!DEX_DIR!"
        mkdir "!JAVA_CLASSES!"
        mkdir "!DEX_DIR!"

        set "JAVAC_LOG=!BUILD_DIR!\javac.log"
        javac -d "!JAVA_CLASSES!" -classpath "!ANDROID_JAR!" @"!JAVA_LIST!" > "!JAVAC_LOG!" 2>&1
        if !errorlevel! neq 0 (
            type "!JAVAC_LOG!"
            echo !R![ERR ]!N! javac failed
            popd
            exit /b 1
        )
        type "!JAVAC_LOG!"

        powershell -NoProfile -Command "$classes = Get-ChildItem -Path '!JAVA_CLASSES!' -Recurse -Filter *.class ^| Select-Object -ExpandProperty FullName; $a = @('--output', '!DEX_DIR!', '--lib', '!ANDROID_JAR!', '--min-api', '!D8_MIN_API!') + $classes; ^& '!BUILD_TOOLS!\d8.bat' @a"
        if !errorlevel! neq 0 (
            echo !R![ERR ]!N! d8 failed
            popd
            exit /b 1
        )
        if not exist "!DEX_DIR!\classes.dex" (
            echo !R![ERR ]!N! d8 failed to produce classes.dex
            popd
            exit /b 1
        )
        for %%f in ("!DEX_DIR!\classes*.dex") do (
            copy /y "%%f" "apk\" >nul
            echo   !D!%%~nxf : %%~zf bytes!N!
        )
    )
)

REM ---- libc++_shared.so ---------------------------------------------------
if "!ABI!"=="arm64-v8a"   set "TRIPLE=aarch64-linux-android"
if "!ABI!"=="armeabi-v7a" set "TRIPLE=arm-linux-androideabi"
if "!ABI!"=="x86"         set "TRIPLE=i686-linux-android"
if "!ABI!"=="x86_64"      set "TRIPLE=x86_64-linux-android"

set "TOOLCHAIN_DIR=!ANDROID_NDK!\toolchains\llvm\prebuilt\!HOST_TAG!"
if exist "!TOOLCHAIN_DIR!" (
    set "LIBCXX=!TOOLCHAIN_DIR!\sysroot\usr\lib\!TRIPLE!\libc++_shared.so"
    if exist "!LIBCXX!" (
        copy /y "!LIBCXX!" "apk\lib\!ABI!\" >nul
        echo   !D!Bundled libc++_shared.so!N!
    )
)

REM ---- aapt2 link ---------------------------------------------------------
"!BUILD_TOOLS!\aapt2.exe" link -I "!ANDROID_JAR!" ^
    --manifest "!MANIFEST_PATH!" --rename-manifest-package "!PACKAGE_NAME!" ^
    --min-sdk-version "!MIN_SDK!" --target-sdk-version "!TARGET_SDK!" ^
    --version-code "!VERSION_CODE!" --version-name "!VERSION_NAME!" ^
    -o knst_app-unsigned.apk
if !errorlevel! neq 0 (
    echo !R![ERR ]!N! aapt2 link failed
    popd
    exit /b 1
)

pushd apk
"!BUILD_TOOLS!\zip.exe" -q -r ..\knst_app-unsigned.apk . >nul 2>&1
REM Fallback: use PowerShell Compress-Archive or standard zip if available
if !errorlevel! neq 0 (
    echo !B![INFO]!N!  Using PowerShell for zip
    powershell -NoProfile -Command "Compress-Archive -Path * -DestinationPath '..\knst_app-unsigned.apk' -Force"
)
popd
if exist apk rmdir /s /q apk

"!BUILD_TOOLS!\zipalign.exe" -f 4 knst_app-unsigned.apk knst_app-aligned.apk
if !errorlevel! neq 0 (
    echo !R![ERR ]!N! zipalign failed
    popd
    exit /b 1
)

if not exist "%USERPROFILE%\.android\debug.keystore" (
    echo !B![INFO]!N!  Generating debug keystore
    if not exist "%USERPROFILE%\.android" mkdir "%USERPROFILE%\.android"
    keytool -genkeypair -v -keystore "%USERPROFILE%\.android\debug.keystore" ^
        -storepass android -keypass android -alias androiddebugkey ^
        -keyalg RSA -keysize 2048 -validity 10000 ^
        -dname "CN=Android Debug,O=Android,C=US" >nul 2>&1
)

"!BUILD_TOOLS!\apksigner.bat" sign --ks "%USERPROFILE%\.android\debug.keystore" ^
    --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey ^
    --min-sdk-version "!MIN_SDK!" --max-sdk-version "!TARGET_SDK!" ^
    --out knst_app.apk knst_app-aligned.apk
if !errorlevel! neq 0 (
    echo !R![ERR ]!N! apksigner failed
    popd
    exit /b 1
)
del /q knst_app-unsigned.apk knst_app-aligned.apk >nul 2>&1
for %%f in (knst_app.apk) do set "APK_SIZE=%%~zf"
set /a APK_SIZE_KB=!APK_SIZE! / 1024
echo   !D!APK size     : !APK_SIZE_KB! KB!N!
echo !G![ OK ]!N!  APK created

popd

REM =============================================================================
REM  [7/8] Deploy
REM =============================================================================
echo.
echo !M!^>!N! !W![7/8] Deploy!N!

call :is_ready "!ANDROID_SERIAL!"
if !errorlevel! neq 0 (
    echo !Y![WARN]!N!  Device lost before install - rediscovering...
    set "ANDROID_SERIAL="
    call :find_device
    if not defined ANDROID_SERIAL (
        echo !R![ERR ]!N! Cannot re-acquire device
        exit /b 1
    )
)

echo !B![INFO]!N!  Uninstalling previous version
adb -s "!ANDROID_SERIAL!" uninstall "!PACKAGE_NAME!" >nul 2>&1

echo !B![INFO]!N!  Installing APK (runtime permissions auto-granted)
call :get_unix_time T2
adb -s "!ANDROID_SERIAL!" install --no-incremental -r -t -g "!BUILD_DIR!\knst_app.apk"
if !errorlevel! neq 0 (
    echo !R![ERR ]!N! install failed
    exit /b 1
)
call :get_unix_time T3
set /a INSTALL_TIME=!T3! - !T2!
echo   !D!Install time : !INSTALL_TIME!s!N!

echo !B![INFO]!N!  Granting special permissions (best-effort)
adb -s "!ANDROID_SERIAL!" shell appops set "!PACKAGE_NAME!" MANAGE_EXTERNAL_STORAGE allow >nul 2>&1

adb -s "!ANDROID_SERIAL!" logcat -c >nul 2>&1

echo !B![INFO]!N!  Launching activity
adb -s "!ANDROID_SERIAL!" shell am start -n "!PACKAGE_NAME!/!ACTIVITY_NAME!" >nul 2>&1
if !errorlevel! equ 0 (
    echo !G![ OK ]!N!  Application launched
) else (
    echo !R![ERR ]!N! Failed to launch activity
)

REM =============================================================================
REM  [8/8] Live logcat
REM =============================================================================
call :get_unix_time TOTAL_END
set /a TOTAL_SECONDS=!TOTAL_END! - !START_TICKS!

echo.
echo !D!-----------------------------------------------------------!N!
echo   !G!+ BUILD . PACKAGE . DEPLOY . LAUNCH - COMPLETE!N!
echo !D!-----------------------------------------------------------!N!
echo   !D!Device        :!N! !DEVICE_MODEL! (Android !DEVICE_ANDROID! / API !DEVICE_SDK!)
echo   !D!Serial        :!N! !ANDROID_SERIAL!
echo   !D!Source        :!N! !SOURCE_FILE!
echo   !D!ABI           :!N! !ABI!
echo   !D!Package       :!N! !PACKAGE_NAME!
echo   !D!SDK / NDK     :!N! !TARGET_SDK! / !NDK_VERSION!
echo   !D!d8 min-api    :!N! !D8_MIN_API!
echo   !D!Scan          :!N! disabled (no ML Kit)
echo   !D!MP3           :!N! native (minimp3.h - no .so)
echo   !D!APK           :!N! build-android-simple\knst_app.apk (!APK_SIZE_KB! KB)
echo   !D!Total elapsed :!N! !TOTAL_SECONDS!s
echo.
echo   !Y!Live logcat starting - press Ctrl+C to stop!N!
echo !D!-----------------------------------------------------------!N!
echo.

adb -s "!ANDROID_SERIAL!" logcat -v color KNST:V AndroidRuntime:E libc:E DEBUG:E "*:S"

endlocal
exit /b 0

REM =============================================================================
REM  Subroutines
REM =============================================================================

:is_ready
REM %1 = serial. Sets ERRORLEVEL 0 if ready, 1 if not.
if "%~1"=="" exit /b 1
adb -s "%~1" get-state 2>nul | findstr /x /c:"device" >nul
if !errorlevel! neq 0 exit /b 1
exit /b 0

:find_device
REM Sets ANDROID_SERIAL. Returns 0 on success, 1 on failure.
set "ANDROID_SERIAL="
set "SERIAL="

REM 1) USB (no ":" and no "_adb-tls")
for /f "skip=1 tokens=1,2" %%a in ('adb devices 2^>nul') do (
    if not defined SERIAL if "%%b"=="device" (
        set "dev=%%a"
        if "!dev::=!"=="!dev!" (
            if "!dev:_adb-tls=!"=="!dev!" (
                set "SERIAL=!dev!"
            )
        )
    )
)
if defined SERIAL (
    echo !B![INFO]!N!  USB device: !SERIAL!
    adb -s "!SERIAL!" tcpip !PREFER_ADB_PORT! >nul 2>&1
    timeout /t 2 /nobreak >nul
    set "IP="
    for /f "tokens=2" %%i in ('adb -s "!SERIAL!" shell ip route 2^>nul ^| findstr /c:" src "') do (
        if not defined IP (
            for /f "tokens=1" %%j in ("%%i") do (
                REM This is the "src <IP>" line; grab token after "src"
            )
        )
    )
    REM Fallback: read wlan0 addr
    if not defined IP (
        for /f "tokens=2" %%i in ('adb -s "!SERIAL!" shell ip -f inet addr show wlan0 2^>nul ^| findstr /c:"inet "') do (
            if not defined IP (
                for /f "tokens=1 delims=/" %%j in ("%%i") do set "IP=%%j"
            )
        )
    )
    if defined IP (
        >"!CACHE_WIFI!" echo !IP!:!PREFER_ADB_PORT!
        echo   !D!Wi-Fi IP cached: !IP!:!PREFER_ADB_PORT!!N!
    )
    set "ANDROID_SERIAL=!SERIAL!"
    exit /b 0
)

REM 2) WiFi IP:PORT
for /f "skip=1 tokens=1,2" %%a in ('adb devices 2^>nul') do (
    if not defined SERIAL if "%%b"=="device" (
        set "dev=%%a"
        if not "!dev::=!"=="!dev!" (
            if "!dev:_adb-tls=!"=="!dev!" set "SERIAL=!dev!"
        )
    )
)
if defined SERIAL (
    call :is_ready "!SERIAL!"
    if !errorlevel!==0 (
        echo !G![ OK ]!N!  WiFi device: !SERIAL!
        >"!CACHE_SERIAL!" echo !SERIAL!
        set "ANDROID_SERIAL=!SERIAL!"
        exit /b 0
    )
    set "SERIAL="
)

REM 3) mDNS
for /f "skip=1 tokens=1,2" %%a in ('adb devices 2^>nul') do (
    if not defined SERIAL if "%%b"=="device" (
        set "dev=%%a"
        if not "!dev:_adb-tls=!"=="!dev!" set "SERIAL=!dev!"
    )
)
if defined SERIAL (
    call :is_ready "!SERIAL!"
    if !errorlevel!==0 (
        echo !G![ OK ]!N!  mDNS device: !SERIAL!
        >"!CACHE_SERIAL!" echo !SERIAL!
        set "ANDROID_SERIAL=!SERIAL!"
        exit /b 0
    )
    set "SERIAL="
)

REM 4) Cached serial
if exist "!CACHE_SERIAL!" (
    set "CACHED="
    for /f "usebackq delims=" %%i in ("!CACHE_SERIAL!") do if not defined CACHED set "CACHED=%%i"
    if defined CACHED (
        call :is_ready "!CACHED!"
        if !errorlevel!==0 (
            echo !G![ OK ]!N!  Cached device: !CACHED!
            set "ANDROID_SERIAL=!CACHED!"
            exit /b 0
        )
        echo !Y![WARN]!N!  Cached serial no longer valid
    )
)

REM 5) Cached WiFi
if exist "!CACHE_WIFI!" (
    set "WIP="
    for /f "usebackq delims=" %%i in ("!CACHE_WIFI!") do if not defined WIP set "WIP=%%i"
    if defined WIP (
        echo !B![INFO]!N!  Trying cached Wi-Fi: !WIP!
        adb connect "!WIP!" >nul 2>&1
        timeout /t 1 /nobreak >nul
        call :is_ready "!WIP!"
        if !errorlevel!==0 (
            echo !G![ OK ]!N!  Wi-Fi device: !WIP!
            >"!CACHE_SERIAL!" echo !WIP!
            set "ANDROID_SERIAL=!WIP!"
            exit /b 0
        )
    )
)

REM 6) Wait for any device (10 s)
echo !B![INFO]!N!  Waiting for mDNS discovery (10 s)...
for /l %%n in (1,1,10) do (
    if not defined ANDROID_SERIAL (
        for /f "skip=1 tokens=1,2" %%a in ('adb devices 2^>nul') do (
            if not defined ANDROID_SERIAL if "%%b"=="device" set "ANDROID_SERIAL=%%a"
        )
        if not defined ANDROID_SERIAL (
            <nul set /p "=."
            timeout /t 1 /nobreak >nul
        ) else (
            echo.
            echo !G![ OK ]!N!  Device appeared: !ANDROID_SERIAL!
            >"!CACHE_SERIAL!" echo !ANDROID_SERIAL!
        )
    )
)
if defined ANDROID_SERIAL exit /b 0
echo.
exit /b 1

:get_unix_time
REM %1 = variable name to store unix seconds in
set "%~1=0"
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "[int][double]::Parse((Get-Date -UFormat %%s))"`) do set "%~1=%%i"
exit /b 0