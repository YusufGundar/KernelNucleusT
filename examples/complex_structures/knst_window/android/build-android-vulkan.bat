@echo off
:: ============================================================================
::  KernelNucleusT - Android (Vulkan) Build, Package & Deploy [Windows]
:: ============================================================================
::
::  Vulkan needs minSdkVersion >= 24 (Android 7.0). The script bails out
::  early if the manifest declares anything lower.
::
::  Pipeline:
::    [1/8] Environment       - SDK, NDK, tools, host
::    [2/8] ADB server        - start or reuse
::    [3/8] Device discovery  - USB / mDNS / cached / wait
::    [4/8] CMake configure   - skipped when nothing changed
::    [5/8] Native build      - parallel ninja
::    [6/8] APK packaging     - aapt2 + javac + d8 + zipalign + apksigner
::    [7/8] Deploy            - uninstall -> install -> launch
::    [8/8] Live logcat       - Ctrl+C to stop
::
:: ============================================================================

setlocal enabledelayedexpansion

chcp 65001 >nul 2>&1
reg add HKCU\Console /v VirtualTerminalLevel /t REG_DWORD /d 1 /f >nul 2>&1
for /F %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"

set "R=%ESC%[0;31m"
set "G=%ESC%[0;32m"
set "Y=%ESC%[1;33m"
set "B=%ESC%[0;34m"
set "M=%ESC%[0;35m"
set "C=%ESC%[0;36m"
set "W=%ESC%[1;37m"
set "D=%ESC%[0;90m"
set "N=%ESC%[0m"

:: ----------------------------------------------------------------------------
::  Configuration
:: ----------------------------------------------------------------------------
set "SOURCE_FILE=knst_window_android_vulkan_test.cpp"
set "PACKAGE_NAME=com.knst.test.vk"
set "ACTIVITY_NAME=com.knst.window.KnstActivity"
set "TARGET_NAME=knst_app"
set "VERSION_CODE=1"
set "VERSION_NAME=1.0.0"
set "PREFER_ADB_PORT=5555"
set "ENABLE_OPENGL=OFF"
set "ENABLE_VULKAN=ON"

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..\..\..\..") do set "PROJECT_ROOT=%%~fI"
set "SOURCE_PATH=%SCRIPT_DIR%\%SOURCE_FILE%"
set "MANIFEST_PATH=%SCRIPT_DIR%\AndroidManifest.xml"
set "BUILD_DIR=%PROJECT_ROOT%\build-android-vulkan"
set "CACHE_SERIAL=%USERPROFILE%\.knst_android_serial"
set "CACHE_WIFI=%USERPROFILE%\.knst_android_wifi"

:: ----------------------------------------------------------------------------
::  Banner
:: ----------------------------------------------------------------------------
echo.
echo %C%+===============================================================+%N%
echo %C%^|%N%  %WKernelNucleusT - Android Build ^& Deploy (Vulkan)%N%           %C%^|%N%
echo %C%+===============================================================+%N%
echo   %DSource :%N% %SOURCE_FILE%
echo   %DPackage:%N% %PACKAGE_NAME%
echo   %DHost   :%N% windows-x86_64
echo   %DBackend:%N% Vulkan 1.x
echo.

:: ============================================================================
::  [1/8] Environment
:: ============================================================================
echo %M%[1/8]%N% %WEnvironment%N%

if not exist "%SOURCE_PATH%" (
    echo   %R%[ERR ]%N%  Source not found: %SOURCE_PATH%
    exit /b 1
)
if not exist "%MANIFEST_PATH%" (
    echo   %R%[ERR ]%N%  Manifest not found: %MANIFEST_PATH%
    exit /b 1
)

for %%T in (adb cmake ninja zip unzip java javac keytool) do (
    where %%T >nul 2>&1
    if errorlevel 1 (
        echo   %R%[ERR ]%N%  Missing tool: %%T
        exit /b 1
    )
)
echo   %G%[ OK ]%N%  Required tools present

if not defined ANDROID_HOME (
    if exist "%LOCALAPPDATA%\Android\Sdk"                   set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
    if not defined ANDROID_HOME if exist "%USERPROFILE%\Android\Sdk" set "ANDROID_HOME=%USERPROFILE%\Android\Sdk"
    if not defined ANDROID_HOME if exist "%USERPROFILE%\AppData\Local\Android\Sdk" set "ANDROID_HOME=%USERPROFILE%\AppData\Local\Android\Sdk"
    if not defined ANDROID_HOME if exist "C:\Android\Sdk"    set "ANDROID_HOME=C:\Android\Sdk"
)
if not defined ANDROID_HOME (
    echo   %R%[ERR ]%N%  Android SDK not found. Set ANDROID_HOME.
    exit /b 1
)
if not exist "%ANDROID_HOME%" (
    echo   %R%[ERR ]%N%  ANDROID_HOME does not exist: %ANDROID_HOME%
    exit /b 1
)

if not defined ANDROID_NDK (
    for /f "delims=" %%D in ('dir /b /ad /o-n "%ANDROID_HOME%\ndk" 2^>nul') do (
        if not defined ANDROID_NDK set "ANDROID_NDK=%ANDROID_HOME%\ndk\%%D"
    )
)
if not defined ANDROID_NDK (
    echo   %R%[ERR ]%N%  No NDK under %ANDROID_HOME%\ndk
    exit /b 1
)
for %%D in ("%ANDROID_NDK%") do set "NDK_VERSION=%%~nxD"

if not defined BUILD_TOOLS (
    for /f "delims=" %%D in ('dir /b /ad /o-n "%ANDROID_HOME%\build-tools" 2^>nul') do (
        if not defined BUILD_TOOLS set "BUILD_TOOLS=%ANDROID_HOME%\build-tools\%%D"
    )
)
if not defined BUILD_TOOLS (
    echo   %R%[ERR ]%N%  No build-tools under %ANDROID_HOME%\build-tools
    exit /b 1
)
for %%D in ("%BUILD_TOOLS%") do set "BUILD_TOOLS_VERSION=%%~nxD"

if not defined KNST_TARGET_SDK (
    if exist "%ANDROID_HOME%\platforms\android-34\android.jar" set "KNST_TARGET_SDK=34"
    if not defined KNST_TARGET_SDK if exist "%ANDROID_HOME%\platforms\android-33\android.jar" set "KNST_TARGET_SDK=33"
)
if not defined KNST_TARGET_SDK (
    for /f "delims=" %%D in ('dir /b /ad /o-n "%ANDROID_HOME%\platforms" 2^>nul') do (
        if not defined KNST_TARGET_SDK (
            set "P=%%D"
            set "KNST_TARGET_SDK=!P:android-=!"
        )
    )
)
if not defined KNST_TARGET_SDK (
    echo   %R%[ERR ]%N%  No Android platform under %ANDROID_HOME%\platforms
    exit /b 1
)
set "ANDROID_JAR=%ANDROID_HOME%\platforms\android-%KNST_TARGET_SDK%\android.jar"
if not exist "!ANDROID_JAR!" (
    echo   %R%[ERR ]%N%  android.jar missing for API %KNST_TARGET_SDK%
    exit /b 1
)

set "MIN_SDK=24"
for /f "tokens=2 delims=\"" %%A in ('findstr /C:"minSdkVersion" "%MANIFEST_PATH%"') do (
    set "MIN_SDK=%%A"
    goto :min_sdk_done
)
:min_sdk_done

:: Vulkan needs API 24 or newer
if %MIN_SDK% LSS 24 (
    echo   %R%[ERR ]%N%  Vulkan requires minSdkVersion ^>= 24.  Current: %MIN_SDK%
    exit /b 1
)

for /f "tokens=*" %%V in ('adb version 2^>nul ^| findstr /R "^Android"') do set "ADB_VERSION_LINE=%%V"

echo   %D%        SDK          : %ANDROID_HOME%%N%
echo   %D%        NDK          : %NDK_VERSION%%N%
echo   %D%        Build tools  : %BUILD_TOOLS_VERSION%%N%
echo   %D%        Target SDK   : %KNST_TARGET_SDK%  (min %MIN_SDK%)%N%
echo   %D%        ADB          : %ADB_VERSION_LINE%%N%
echo   %G%[ OK ]%N%  Environment ready

:: ============================================================================
::  [2/8] ADB server
:: ============================================================================
echo.
echo %M%[2/8]%N% %WADB server%N%

adb start-server >nul 2>&1
set "EXISTING_DEV="
for /f "skip=1 tokens=1,2" %%A in ('adb devices') do (
    if "%%B"=="device" (
        if not defined EXISTING_DEV set "EXISTING_DEV=%%A"
    )
)
if defined EXISTING_DEV (
    echo   %G%[ OK ]%N%  ADB running - device attached: !EXISTING_DEV!
) else (
    echo   %B%[INFO]%N%  No device attached; restarting ADB
    adb kill-server >nul 2>&1
    adb start-server >nul 2>&1
    ping -n 3 127.0.0.1 >nul 2>&1
)

:: ============================================================================
::  [3/8] Device discovery
:: ============================================================================
echo.
echo %M%[3/8]%N% %WDevice discovery%N%

set "ANDROID_SERIAL="

:: 1) USB
for /f "skip=1 tokens=1,2" %%A in ('adb devices') do (
    if "%%B"=="device" (
        set "T=%%A"
        if not defined ANDROID_SERIAL (
            if "!T:~0,1!" NEQ ":" (
                if "!T:_adb-tls-connect=!"=="!T!" (
                    set "ANDROID_SERIAL=%%A"
                )
            )
        )
    )
)
if defined ANDROID_SERIAL echo   %B%[INFO]%N%  USB device: !ANDROID_SERIAL!

:: 2) mDNS
if not defined ANDROID_SERIAL (
    for /f "skip=1 tokens=1,2" %%A in ('adb devices') do (
        if "%%B"=="device" (
            set "T=%%A"
            if not defined ANDROID_SERIAL (
                if "!T:_adb-tls-connect=!" NEQ "!T!" set "ANDROID_SERIAL=%%A"
            )
        )
    )
    if defined ANDROID_SERIAL echo   %G%[ OK ]%N%  mDNS device: !ANDROID_SERIAL!
)

:: 3) Cached serial
if not defined ANDROID_SERIAL if exist "%CACHE_SERIAL%" (
    set /p "CACHED="<"%CACHE_SERIAL%"
    if defined CACHED (
        for /f "skip=1 tokens=1,2" %%A in ('adb devices') do (
            if "%%B"=="device" if "%%A"=="!CACHED!" set "ANDROID_SERIAL=!CACHED!"
        )
        if defined ANDROID_SERIAL echo   %G%[ OK ]%N%  Cached device: !ANDROID_SERIAL!
    )
)

:: 4) Wait for mDNS
if not defined ANDROID_SERIAL (
    echo   %B%[INFO]%N%  Waiting for a device (10s^)...
    for /L %%I in (1,1,10) do (
        if not defined ANDROID_SERIAL (
            for /f "skip=1 tokens=1,2" %%A in ('adb devices') do (
                if "%%B"=="device" if not defined ANDROID_SERIAL set "ANDROID_SERIAL=%%A"
            )
        )
        if not defined ANDROID_SERIAL (
            <nul set /p "=."
            ping -n 2 127.0.0.1 >nul 2>&1
        )
    )
    echo.
)

if not defined ANDROID_SERIAL (
    echo   %R%[ERR ]%N%  No device found.
    echo.
    echo   %Y%How to connect a device%N%
    echo     Option 1 - USB cable
    echo       * Plug the phone in
    echo       * Enable Developer Options -^> USB Debugging
    echo       * Re-run this script
    echo.
    echo     Option 2 - Wireless
    echo       * Phone -^> Developer Options -^> Wireless Debugging
    echo       * adb pair IP:PAIR_PORT
    echo       * adb connect IP:PORT
    echo.
    adb devices -l
    exit /b 1
)

echo !ANDROID_SERIAL!>"%CACHE_SERIAL%"

for /f "tokens=*" %%M in ('adb -s %ANDROID_SERIAL% shell getprop ro.product.model 2^>nul') do set "DEVICE_MODEL=%%M"
for /f "tokens=*" %%M in ('adb -s %ANDROID_SERIAL% shell getprop ro.build.version.release 2^>nul') do set "DEVICE_ANDROID=%%M"
for /f "tokens=*" %%M in ('adb -s %ANDROID_SERIAL% shell getprop ro.product.cpu.abi 2^>nul') do set "DEVICE_ABI=%%M"
for /f "tokens=*" %%M in ('adb -s %ANDROID_SERIAL% shell getprop ro.build.version.sdk 2^>nul') do set "DEVICE_SDK=%%M"

if not defined DEVICE_MODEL   set "DEVICE_MODEL=(unknown)"
if not defined DEVICE_ANDROID set "DEVICE_ANDROID=?"
if not defined DEVICE_SDK     set "DEVICE_SDK=?"

set "ABI=%DEVICE_ABI%"
if defined KNST_ABI set "ABI=%KNST_ABI%"
if not defined ABI set "ABI=arm64-v8a"

echo   %D%        Model        : %DEVICE_MODEL%%N%
echo   %D%        Android      : %DEVICE_ANDROID% (API %DEVICE_SDK%)%N%
echo   %D%        Device ABI   : %DEVICE_ABI%%N%
echo   %D%        Build ABI    : %ABI%%N%
echo   %D%        Serial       : %ANDROID_SERIAL%%N%
echo   %G%[ OK ]%N%  Device ready

:: ============================================================================
::  [4/8] CMake configure
:: ============================================================================
echo.
echo %M%[4/8]%N% %WCMake configure%N%

set "SRC_CACHE=%BUILD_DIR%\.knst_last_source"
set "CFG_CACHE=%BUILD_DIR%\.knst_last_config"
set "CURRENT_CFG=%ABI%|%MIN_SDK%|gl=%ENABLE_OPENGL%|vk=%ENABLE_VULKAN%|%SOURCE_PATH%"

set "NEED_CONFIGURE=0"
if not exist "%BUILD_DIR%\build.ninja" set "NEED_CONFIGURE=1"
if not exist "%SRC_CACHE%" set "NEED_CONFIGURE=1"
if not exist "%CFG_CACHE%" set "NEED_CONFIGURE=1"

if "!NEED_CONFIGURE!"=="0" (
    set /p "LAST_SRC="<"%SRC_CACHE%"
    set /p "LAST_CFG="<"%CFG_CACHE%"
    if not "!LAST_SRC!"=="%SOURCE_PATH%" set "NEED_CONFIGURE=1"
    if not "!LAST_CFG!"=="%CURRENT_CFG%" set "NEED_CONFIGURE=1"
)

if "!NEED_CONFIGURE!"=="1" (
    echo   %B%[INFO]%N%  Running cmake configure...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    mkdir "%BUILD_DIR%" >nul

    cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_TOOLCHAIN_FILE="%ANDROID_NDK%\build\cmake\android.toolchain.cmake" ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DANDROID_ABI=%ABI% ^
        -DANDROID_PLATFORM=android-%MIN_SDK% ^
        -DKNST_WINDOW_USE_OPENGL=%ENABLE_OPENGL% ^
        -DKNST_WINDOW_USE_VULKAN=%ENABLE_VULKAN% ^
        -DKNST_APP_SOURCE="%SOURCE_PATH%"

    if errorlevel 1 (
        echo   %R%[ERR ]%N%  CMake configure failed
        exit /b 1
    )

    >"%SRC_CACHE%" echo %SOURCE_PATH%
    >"%CFG_CACHE%" echo %CURRENT_CFG%
    echo   %G%[ OK ]%N%  Configured
) else (
    echo   %G%[ OK ]%N%  Build directory up-to-date
)

:: ============================================================================
::  [5/8] Native build
:: ============================================================================
echo.
echo %M%[5/8]%N% %WNative build%N%

cmake --build "%BUILD_DIR%" -j
if errorlevel 1 (
    echo   %R%[ERR ]%N%  Build failed
    exit /b 1
)

set "LIB_PATH=%BUILD_DIR%\lib%TARGET_NAME%.so"
if not exist "!LIB_PATH!" (
    echo   %R%[ERR ]%N%  Shared library missing: lib%TARGET_NAME%.so
    exit /b 1
)
for %%F in ("!LIB_PATH!") do set "LIB_SIZE_KB=%%~zF"
set /a LIB_SIZE_MB=!LIB_SIZE_KB! / 1024 / 1024
echo   %D%        Library size : ~!LIB_SIZE_MB! MB%N%
echo   %G%[ OK ]%N%  Build complete

:: ============================================================================
::  [6/8] APK packaging
:: ============================================================================
echo.
echo %M%[6/8]%N% %WAPK packaging%N%

pushd "%BUILD_DIR%"

if exist knst_app.apk del /q knst_app.apk
if exist knst_app-unsigned.apk del /q knst_app-unsigned.apk
if exist knst_app-aligned.apk del /q knst_app-aligned.apk
if exist apk rmdir /s /q apk
mkdir "apk\lib\%ABI%" >nul

copy /y "lib%TARGET_NAME%.so" "apk\lib\%ABI%\" >nul

:: ─── Java -> DEX (IME bridge) ─────────────────────────────────────────
set "JAVA_SRC_DIR=%PROJECT_ROOT%\include\platform\android"
if exist "%JAVA_SRC_DIR%" (
    echo   %B%[INFO]%N%  Compiling Java sources (IME bridge^)
    if exist "java-classes" rmdir /s /q "java-classes"
    if exist "dex" rmdir /s /q "dex"
    mkdir "java-classes" >nul
    mkdir "dex" >nul

    javac -d "java-classes" -classpath "%ANDROID_JAR%" "%JAVA_SRC_DIR%\KnstActivity.java"
    if errorlevel 1 (
        echo   %R%[ERR ]%N%  javac failed
        popd
        exit /b 1
    )

    set "CLASSES="
    for /r "java-classes" %%C in (*.class) do set "CLASSES=!CLASSES! "%%C""

    call "%BUILD_TOOLS%\d8.bat" --output "dex" --lib "%ANDROID_JAR%" --min-api %MIN_SDK% !CLASSES!
    if errorlevel 1 (
        echo   %R%[ERR ]%N%  d8 failed
        popd
        exit /b 1
    )

    if exist "dex\classes.dex" (
        copy /y "dex\classes.dex" "apk\classes.dex" >nul
        for %%F in ("apk\classes.dex") do set "DEX_SIZE=%%~zF"
        echo   %D%        classes.dex : !DEX_SIZE! bytes%N%
    ) else (
        echo   %R%[ERR ]%N%  d8 did not produce classes.dex
        popd
        exit /b 1
    )
)

:: ─── libc++_shared.so ────────────────────────────────────────────────
if "%ABI%"=="arm64-v8a"   set "TRIPLE=aarch64-linux-android"
if "%ABI%"=="armeabi-v7a" set "TRIPLE=arm-linux-androideabi"
if "%ABI%"=="x86"         set "TRIPLE=i686-linux-android"
if "%ABI%"=="x86_64"      set "TRIPLE=x86_64-linux-android"

set "LIBCXX=%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\%TRIPLE%\libc++_shared.so"
if exist "%LIBCXX%" (
    copy /y "%LIBCXX%" "apk\lib\%ABI%\" >nul
    echo   %D%        Bundled libc++_shared.so%N%
)

:: ─── aapt2 link ──────────────────────────────────────────────────────
"%BUILD_TOOLS%\aapt2.exe" link -I "%ANDROID_JAR%" ^
    --manifest "%MANIFEST_PATH%" ^
    --rename-manifest-package %PACKAGE_NAME% ^
    --min-sdk-version %MIN_SDK% ^
    --target-sdk-version %KNST_TARGET_SDK% ^
    --version-code %VERSION_CODE% ^
    --version-name %VERSION_NAME% ^
    -o knst_app-unsigned.apk
if errorlevel 1 (
    echo   %R%[ERR ]%N%  aapt2 link failed
    popd
    exit /b 1
)

pushd apk
zip -q -r ..\knst_app-unsigned.apk *
popd
rmdir /s /q apk

"%BUILD_TOOLS%\zipalign.exe" -f 4 knst_app-unsigned.apk knst_app-aligned.apk
if errorlevel 1 (
    echo   %R%[ERR ]%N%  zipalign failed
    popd
    exit /b 1
)

if not exist "%USERPROFILE%\.android\debug.keystore" (
    echo   %B%[INFO]%N%  Generating debug keystore
    if not exist "%USERPROFILE%\.android" mkdir "%USERPROFILE%\.android"
    keytool -genkeypair -v ^
        -keystore "%USERPROFILE%\.android\debug.keystore" ^
        -storepass android -keypass android ^
        -alias androiddebugkey ^
        -keyalg RSA -keysize 2048 -validity 10000 ^
        -dname "CN=Android Debug,O=Android,C=US" >nul 2>&1
)

call "%BUILD_TOOLS%\apksigner.bat" sign ^
    --ks "%USERPROFILE%\.android\debug.keystore" ^
    --ks-pass pass:android --key-pass pass:android ^
    --ks-key-alias androiddebugkey ^
    --out knst_app.apk knst_app-aligned.apk
if errorlevel 1 (
    echo   %R%[ERR ]%N%  apksigner failed
    popd
    exit /b 1
)

del /q knst_app-unsigned.apk knst_app-aligned.apk 2>nul

for %%F in ("knst_app.apk") do set "APK_SIZE=%%~zF"
set /a APK_SIZE_KB=!APK_SIZE! / 1024
echo   %D%        APK size     : ~!APK_SIZE_KB! KB%N%
echo   %G%[ OK ]%N%  APK created

popd

:: ============================================================================
::  [7/8] Deploy
:: ============================================================================
echo.
echo %M%[7/8]%N% %WDeploy%N%

echo   %B%[INFO]%N%  Uninstalling previous version
adb -s %ANDROID_SERIAL% uninstall %PACKAGE_NAME% >nul 2>&1

echo   %B%[INFO]%N%  Installing APK
adb -s %ANDROID_SERIAL% install -r "%BUILD_DIR%\knst_app.apk"
if errorlevel 1 (
    echo   %R%[ERR ]%N%  Install failed
    exit /b 1
)

adb -s %ANDROID_SERIAL% logcat -c
echo   %B%[INFO]%N%  Launching activity
adb -s %ANDROID_SERIAL% shell am start -n %PACKAGE_NAME%/%ACTIVITY_NAME% >nul 2>&1
if errorlevel 1 (
    echo   %R%[ERR ]%N%  Failed to launch activity
    exit /b 1
)
echo   %G%[ OK ]%N%  Application launched

:: ============================================================================
::  [8/8] Live logcat
:: ============================================================================
echo.
echo %D%------------------------------------------------------------%N%
echo   %G% BUILD . PACKAGE . DEPLOY . LAUNCH - COMPLETE (Vulkan)%N%
echo %D%------------------------------------------------------------%N%
echo   %D%Device   :%N% %DEVICE_MODEL% (Android %DEVICE_ANDROID% / API %DEVICE_SDK%)
echo   %D%Serial   :%N% %ANDROID_SERIAL%
echo   %D%Backend  :%N% Vulkan 1.x
echo   %D%Package  :%N% %PACKAGE_NAME%
echo   %D%ABI      :%N% %ABI%
echo   %D%APK      :%N% build-android-vulkan\knst_app.apk
echo.
echo   %Y%Live logcat starting - press Ctrl+C to stop%N%
echo %D%------------------------------------------------------------%N%
echo.

adb -s %ANDROID_SERIAL% logcat -v color KNST:V AndroidRuntime:E libc:E DEBUG:E *:S

endlocal