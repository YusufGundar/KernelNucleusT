@echo off
setlocal EnableDelayedExpansion
REM ============================================================================
REM  KernelNucleusT - Android Build, Package & Deploy  (Windows)
REM ============================================================================
REM
REM  Fully automatic:
REM    . NDK version       -> auto-detected (latest installed)
REM    . Build tools       -> auto-detected (latest installed)
REM    . Target platform   -> auto-detected
REM    . ABI               -> detected from connected device
REM    . libc++_shared     -> located via NDK toolchain probing
REM
REM  Pipeline (8 stages):
REM    [1/8] Environment       - SDK, NDK, tools
REM    [2/8] ADB server        - start / preserve existing
REM    [3/8] Device discovery  - USB / mDNS / cache / Wi-Fi
REM    [4/8] CMake configure   - skipped when nothing changed
REM    [5/8] Native build      - parallel ninja
REM    [6/8] APK packaging     - aapt2 + zipalign + apksigner
REM    [7/8] Deploy            - uninstall -> install -> launch
REM    [8/8] Live logcat       - filtered stream (Ctrl+C to stop)
REM
REM  DEVICE CONNECTION
REM    . USB cable          -> works out of the box
REM    . VSCode extension   -> "Android ADB WLAN - Wireless Debugging"
REM    . Manual wireless    -> adb connect <ip>:<port>
REM    . Cached serial      -> previous successful device
REM    . Cached Wi-Fi IP    -> stored after first USB run
REM
REM  CUSTOMISATION
REM    Only SOURCE_FILE usually needs to change.
REM    Overrides via environment variables:
REM      ANDROID_HOME=C:\path
REM      ANDROID_NDK=C:\path
REM      KNST_ABI=arm64-v8a
REM      KNST_TARGET_SDK=34
REM ============================================================================

REM ---- ANSI colours via PowerShell -------------------------------------------
for /f "delims=" %%a in ('powershell -NoProfile -Command "[char]27" 2^>nul') do set "ESC=%%a"
set "R=%ESC%[91m"
set "G=%ESC%[92m"
set "Y=%ESC%[93m"
set "B=%ESC%[94m"
set "M=%ESC%[95m"
set "C=%ESC%[96m"
set "W=%ESC%[97m"
set "D=%ESC%[90m"
set "N=%ESC%[0m"

REM ============================================================================
REM  CONFIGURATION
REM ============================================================================
set "SOURCE_FILE=knst_window_android_test.cpp"
set "PACKAGE_NAME=com.knst.test"
set "ACTIVITY_NAME=android.app.NativeActivity"
set "TARGET_NAME=knst_app"
set "VERSION_CODE=1"
set "VERSION_NAME=1.0.0"
set "ENABLE_VULKAN=OFF"
set "PREFER_ADB_PORT=5555"

REM ============================================================================
REM  PATHS
REM ============================================================================
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

pushd "%SCRIPT_DIR%\..\..\..\.."
set "PROJECT_ROOT=%CD%"
popd

set "SOURCE_PATH=%SCRIPT_DIR%\%SOURCE_FILE%"
set "MANIFEST_PATH=%SCRIPT_DIR%\AndroidManifest.xml"
set "BUILD_DIR=%PROJECT_ROOT%\build-android"
set "CACHE_SERIAL=%USERPROFILE%\.knst_android_serial"
set "CACHE_WIFI=%USERPROFILE%\.knst_android_wifi"

REM ---- SDK discovery ---------------------------------------------------------
if not defined ANDROID_HOME (
    if exist "%LOCALAPPDATA%\Android\Sdk" (
        set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
    ) else if exist "%USERPROFILE%\AppData\Local\Android\Sdk" (
        set "ANDROID_HOME=%USERPROFILE%\AppData\Local\Android\Sdk"
    ) else if exist "%USERPROFILE%\Android\Sdk" (
        set "ANDROID_HOME=%USERPROFILE%\Android\Sdk"
    ) else if exist "C:\Android\Sdk" (
        set "ANDROID_HOME=C:\Android\Sdk"
    )
)
if not defined ANDROID_HOME (
    echo %R%[ERR ]%N%  Android SDK not found. Set ANDROID_HOME.
    exit /b 1
)

REM ============================================================================
REM  HEADER
REM ============================================================================
echo.
echo %C%+-------------------------------------------------------------------+%N%
echo %C%^|%N%  %W%KernelNucleusT - Android Build ^& Deploy%N%                        %C%^|%N%
echo %C%+-------------------------------------------------------------------+%N%
echo   %D%Source :%N% %SOURCE_FILE%
echo   %D%Package:%N% %PACKAGE_NAME%
echo.

REM ============================================================================
REM  [1/8] ENVIRONMENT
REM ============================================================================
echo %M%^>%N% %W%[1/8] Environment%N%

if not exist "%SOURCE_PATH%" (
    echo %R%[ERR ]%N%  Source file not found: %SOURCE_PATH%
    exit /b 1
)
if not exist "%MANIFEST_PATH%" (
    echo %R%[ERR ]%N%  Manifest not found: %MANIFEST_PATH%
    exit /b 1
)

for %%T in (adb cmake ninja zip keytool) do (
    where %%T >nul 2>&1
    if errorlevel 1 (
        echo %R%[ERR ]%N%  Missing required tool: %%T
        exit /b 1
    )
)
echo %G%[ OK ]%N%  Required tools present

REM ---- NDK auto-detect ------------------------------------------------------
if not defined ANDROID_NDK (
    for /f "delims=" %%d in ('dir /b /ad /o-n "%ANDROID_HOME%\ndk" 2^>nul') do (
        set "ANDROID_NDK=%ANDROID_HOME%\ndk\%%d"
        goto :ndk_done
    )
    :ndk_done
)
if not defined ANDROID_NDK (
    echo %R%[ERR ]%N%  No Android NDK found under %ANDROID_HOME%\ndk
    exit /b 1
)
for %%d in ("%ANDROID_NDK%") do set "NDK_VERSION=%%~nxd"

REM ---- Build tools auto-detect ---------------------------------------------
for /f "delims=" %%d in ('dir /b /ad /o-n "%ANDROID_HOME%\build-tools" 2^>nul') do (
    set "BUILD_TOOLS=%ANDROID_HOME%\build-tools\%%d"
    goto :bt_done
)
:bt_done
if not defined BUILD_TOOLS (
    echo %R%[ERR ]%N%  No build-tools found under %ANDROID_HOME%\build-tools
    exit /b 1
)
for %%d in ("%BUILD_TOOLS%") do set "BUILD_TOOLS_VERSION=%%~nxd"

REM ---- Target SDK auto-detect ----------------------------------------------
if not defined KNST_TARGET_SDK (
    if exist "%ANDROID_HOME%\platforms\android-34\android.jar" (
        set "KNST_TARGET_SDK=34"
    ) else if exist "%ANDROID_HOME%\platforms\android-33\android.jar" (
        set "KNST_TARGET_SDK=33"
    ) else (
        for /f "delims=" %%d in ('dir /b /ad /o-n "%ANDROID_HOME%\platforms" 2^>nul') do (
            set "_plat=%%d"
            set "_plat=!_plat:android-=!"
            set "KNST_TARGET_SDK=!_plat!"
            goto :sdk_done
        )
        :sdk_done
    )
)
if not defined KNST_TARGET_SDK (
    echo %R%[ERR ]%N%  No Android platform installed
    exit /b 1
)

set "ANDROID_JAR=%ANDROID_HOME%\platforms\android-%KNST_TARGET_SDK%\android.jar"
if not exist "%ANDROID_JAR%" (
    echo %R%[ERR ]%N%  android.jar missing for API %KNST_TARGET_SDK%
    exit /b 1
)
set "TARGET_SDK=%KNST_TARGET_SDK%"

REM ---- min SDK from manifest ------------------------------------------------
set "MIN_SDK="
for /f "tokens=2 delims==" %%v in ('findstr /r /c:"minSdkVersion" "%MANIFEST_PATH%" 2^>nul') do (
    set "_raw=%%v"
    set "_raw=!_raw:"=!"
    set "_raw=!_raw: =!"
    set "MIN_SDK=!_raw!"
)
if not defined MIN_SDK set "MIN_SDK=24"

echo   %D%SDK          :%N% %ANDROID_HOME%
echo   %D%NDK          :%N% %NDK_VERSION%
echo   %D%Build tools  :%N% %BUILD_TOOLS_VERSION%
echo   %D%Target SDK   :%N% %TARGET_SDK%  (min %MIN_SDK%)
echo %G%[ OK ]%N%  Environment ready

REM ============================================================================
REM  [2/8] ADB SERVER
REM ============================================================================
echo.
echo %M%^>%N% %W%[2/8] ADB server%N%

set "EXISTING_DEV="
for /f "tokens=1,2" %%a in ('adb devices 2^>nul') do (
    if "%%b"=="device" (
        set "EXISTING_DEV=%%a"
        goto :adb_ready
    )
)
:adb_ready

if defined EXISTING_DEV (
    echo %G%[ OK ]%N%  ADB already running - device attached: !EXISTING_DEV!
) else (
    echo %B%[INFO]%N%  No device attached; restarting ADB with mDNS discovery enabled
    adb kill-server >nul 2>&1
    set "ADB_MDNS_OPENSCREEN=1"
    adb start-server >nul 2>&1
    ping -n 3 127.0.0.1 >nul
)

REM Disconnect stale offline entries only
for /f "tokens=1,2" %%a in ('adb devices') do (
    if "%%b"=="offline" (
        echo %Y%[WARN]%N%  Disconnecting offline device: %%a
        adb disconnect %%a >nul 2>&1
    )
)

REM ============================================================================
REM  [3/8] DEVICE DISCOVERY
REM ============================================================================
echo.
echo %M%^>%N% %W%[3/8] Device discovery%N%

set "ANDROID_SERIAL="

REM ---- 3a. USB --------------------------------------------------------------
for /f "tokens=1,2" %%a in ('adb devices') do (
    if "%%b"=="device" (
        set "_dev=%%a"
        if "!_dev:~0,4!"=="adb-" (
            REM mDNS - skip here
        ) else (
            echo !_dev! | findstr /r /c:":" >nul
            if errorlevel 1 (
                set "USB_DEV=!_dev!"
            )
        )
    )
)

if defined USB_DEV (
    echo %B%[INFO]%N%  USB device: !USB_DEV!
    adb -s "!USB_DEV!" tcpip %PREFER_ADB_PORT% >nul 2>&1
    ping -n 3 127.0.0.1 >nul
    set "USB_IP="
    for /f "tokens=2" %%i in ('adb -s "!USB_DEV!" shell ip route 2^>nul ^| findstr /r /c:"src "') do (
        if not defined USB_IP set "USB_IP=%%i"
    )
    if defined USB_IP (
        echo !USB_IP!:!PREFER_ADB_PORT!> "%CACHE_WIFI%"
        echo         Wi-Fi IP cached: !USB_IP!:!PREFER_ADB_PORT!
    )
    set "ANDROID_SERIAL=!USB_DEV!"
    goto :device_found
)

REM ---- 3b. mDNS -------------------------------------------------------------
set "MDNS_DEV="
for /f "tokens=1,2" %%a in ('adb devices') do (
    if "%%b"=="device" (
        set "_dev=%%a"
        if "!_dev:~0,4!"=="adb-" set "MDNS_DEV=!_dev!"
    )
)
if defined MDNS_DEV (
    echo %G%[ OK ]%N%  mDNS device: !MDNS_DEV!
    echo !MDNS_DEV!> "%CACHE_SERIAL%"
    set "ANDROID_SERIAL=!MDNS_DEV!"
    goto :device_found
)

REM ---- 3c. Cached serial ----------------------------------------------------
if exist "%CACHE_SERIAL%" (
    set /p CACHED=<"%CACHE_SERIAL%"
    adb -s "!CACHED!" get-state 2>nul | findstr /r /c:"^device" >nul
    if not errorlevel 1 (
        echo %G%[ OK ]%N%  Cached device: !CACHED!
        set "ANDROID_SERIAL=!CACHED!"
        goto :device_found
    ) else (
        echo %Y%[WARN]%N%  Cached serial no longer valid
    )
)

REM ---- 3d. Cached Wi-Fi IP --------------------------------------------------
if exist "%CACHE_WIFI%" (
    set /p WIP=<"%CACHE_WIFI%"
    if defined WIP (
        echo %B%[INFO]%N%  Trying cached Wi-Fi: !WIP!
        adb connect "!WIP!" >nul 2>&1
        ping -n 2 127.0.0.1 >nul
        adb -s "!WIP!" get-state 2>nul | findstr /r /c:"^device" >nul
        if not errorlevel 1 (
            echo %G%[ OK ]%N%  Wi-Fi device: !WIP!
            echo !WIP!> "%CACHE_SERIAL%"
            set "ANDROID_SERIAL=!WIP!"
            goto :device_found
        )
    )
)

REM ---- 3e. Grace period for slow mDNS ---------------------------------------
echo %B%[INFO]%N%  Waiting for mDNS discovery (10 s)...
for /l %%i in (1,1,10) do (
    if not defined ANDROID_SERIAL (
        for /f "tokens=1,2" %%a in ('adb devices') do (
            if "%%b"=="device" (
                if not defined ANDROID_SERIAL (
                    set "ANDROID_SERIAL=%%a"
                    echo %%a> "%CACHE_SERIAL%"
                )
            )
        )
        if not defined ANDROID_SERIAL (
            <nul set /p "=."
            ping -n 2 127.0.0.1 >nul
        )
    )
)
echo.

if not defined ANDROID_SERIAL (
    echo %R%[ERR ]%N%  No device found.
    echo.
    echo   %D%--------------------------------------------------------------%N%
    echo   %Y%How to connect a device%N%
    echo   %D%--------------------------------------------------------------%N%
    echo   %W%Option 1 - USB cable%N% (recommended first time)
    echo     . Plug the phone in
    echo     . Enable Developer Options -^> USB Debugging
    echo     . Re-run this script
    echo.
    echo   %W%Option 2 - Wireless (VSCode)%N%
    echo     . Install: Android ADB WLAN - Wireless Debugging
    echo     . Pair + connect from the extension UI
    echo     . Re-run this script
    echo.
    echo   %W%Option 3 - Wireless (manual)%N%
    echo     . Phone -^> Developer Options -^> Wireless Debugging
    echo     . Run: adb pair IP:PAIR_PORT  then  adb connect IP:PORT
    echo.
    echo   Current adb devices:
    adb devices -l
    exit /b 1
)

:device_found
echo.

REM Stability check
set "STABLE=0"
for /l %%i in (1,1,3) do (
    if "!STABLE!"=="0" (
        adb -s "!ANDROID_SERIAL!" get-state 2>nul | findstr /r /c:"^device" >nul
        if errorlevel 1 (
            echo %Y%[WARN]%N%  Device temporarily unavailable (%%i/3)...
            ping -n 2 127.0.0.1 >nul
        ) else (
            set "STABLE=1"
        )
    )
)
if "!STABLE!"=="0" (
    echo %R%[ERR ]%N%  Device not stable: !ANDROID_SERIAL!
    exit /b 1
)

REM Device info
for /f "delims=" %%m in ('adb -s "!ANDROID_SERIAL!" shell getprop ro.product.model 2^>nul') do set "DEVICE_MODEL=%%m"
for /f "delims=" %%m in ('adb -s "!ANDROID_SERIAL!" shell getprop ro.build.version.release 2^>nul') do set "DEVICE_ANDROID=%%m"
for /f "delims=" %%m in ('adb -s "!ANDROID_SERIAL!" shell getprop ro.product.cpu.abi 2^>nul') do set "DEVICE_ABI=%%m"
for /f "delims=" %%m in ('adb -s "!ANDROID_SERIAL!" shell getprop ro.build.version.sdk 2^>nul') do set "DEVICE_SDK=%%m"

if not defined DEVICE_MODEL set "DEVICE_MODEL=(unknown)"
if not defined DEVICE_ANDROID set "DEVICE_ANDROID=?"
if not defined DEVICE_SDK set "DEVICE_SDK=?"

if not defined KNST_ABI set "KNST_ABI=!DEVICE_ABI!"
set "ABI=!KNST_ABI!"
if "!ABI!"=="" set "ABI=arm64-v8a"

echo   %D%Model        :%N% !DEVICE_MODEL!
echo   %D%Android      :%N% !DEVICE_ANDROID! (API !DEVICE_SDK!)
echo   %D%Device ABI   :%N% !DEVICE_ABI!
echo   %D%Build ABI    :%N% !ABI!
echo   %D%Serial       :%N% !ANDROID_SERIAL!
echo %G%[ OK ]%N%  Device ready

REM ============================================================================
REM  [4/8] CMAKE CONFIGURE
REM ============================================================================
echo.
echo %M%^>%N% %W%[4/8] CMake configure%N%

set "SRC_CACHE=%BUILD_DIR%\.knst_last_source"
set "CFG_CACHE=%BUILD_DIR%\.knst_last_config"
set "CURRENT_CFG=%ABI%^|%MIN_SDK%^|%ENABLE_VULKAN%^|%SOURCE_PATH%"

set "NEED_CONFIGURE=0"
if not exist "%BUILD_DIR%\build.ninja" (
    set "NEED_CONFIGURE=1"
) else if not exist "%SRC_CACHE%" (
    set "NEED_CONFIGURE=1"
) else (
    set /p LAST_SRC=<"%SRC_CACHE%"
    if not "!LAST_SRC!"=="%SOURCE_PATH%" (
        echo %B%[INFO]%N%  Source changed - reconfiguring
        set "NEED_CONFIGURE=1"
    )
)

if "!NEED_CONFIGURE!"=="1" (
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_TOOLCHAIN_FILE="%ANDROID_NDK%/build/cmake/android.toolchain.cmake" ^
        -DANDROID_ABI="%ABI%" ^
        -DANDROID_PLATFORM="android-%MIN_SDK%" ^
        -DKNST_ENABLE_VULKAN="%ENABLE_VULKAN%" ^
        -DKNST_APP_SOURCE="%SOURCE_PATH%"
    if errorlevel 1 (
        echo %R%[ERR ]%N%  CMake configure failed
        exit /b 1
    )
    if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
    echo %SOURCE_PATH%> "%SRC_CACHE%"
    echo %CURRENT_CFG%> "%CFG_CACHE%"
    echo %G%[ OK ]%N%  Configured
) else (
    echo %G%[ OK ]%N%  Build directory up-to-date
)

REM ============================================================================
REM  [5/8] NATIVE BUILD
REM ============================================================================
echo.
echo %M%^>%N% %W%[5/8] Native build%N%

set "T0=%TIME%"
cmake --build "%BUILD_DIR%" -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo %R%[ERR ]%N%  Build failed
    exit /b 1
)
set "T1=%TIME%"

set "LIB_PATH=%BUILD_DIR%\lib%TARGET_NAME%.so"
if not exist "%LIB_PATH%" (
    echo %R%[ERR ]%N%  Shared library missing: lib%TARGET_NAME%.so
    exit /b 1
)
for %%f in ("%LIB_PATH%") do set "LIB_SIZE=%%~zf"
echo         Library size : %LIB_SIZE% bytes
echo %G%[ OK ]%N%  Build complete

REM ============================================================================
REM  [6/8] APK PACKAGING
REM ============================================================================
echo.
echo %M%^>%N% %W%[6/8] APK packaging%N%

pushd "%BUILD_DIR%"

del /q knst_app*.apk 2>nul
if exist apk rmdir /s /q apk
mkdir "apk\lib\%ABI%"
copy /y "lib%TARGET_NAME%.so" "apk\lib\%ABI%\" >nul
if errorlevel 1 (
    echo %R%[ERR ]%N%  Failed to stage .so
    popd
    exit /b 1
)

REM libc++_shared - host tag windows-x86_64, ABI-specific path
set "LIBCXX_DIR=%ANDROID_NDK%\toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib"
if "%ABI%"=="arm64-v8a" (
    set "LIBCXX=%LIBCXX_DIR%\aarch64-linux-android\libc++_shared.so"
) else if "%ABI%"=="armeabi-v7a" (
    set "LIBCXX=%LIBCXX_DIR%\arm-linux-androideabi\libc++_shared.so"
) else if "%ABI%"=="x86_64" (
    set "LIBCXX=%LIBCXX_DIR%\x86_64-linux-android\libc++_shared.so"
) else if "%ABI%"=="x86" (
    set "LIBCXX=%LIBCXX_DIR%\i686-linux-android\libc++_shared.so"
)
if exist "%LIBCXX%" (
    copy /y "%LIBCXX%" "apk\lib\%ABI%\" >nul
    echo         Bundled libc++_shared.so
)

"%BUILD_TOOLS%\aapt2.exe" link -I "%ANDROID_JAR%" ^
    --manifest "%MANIFEST_PATH%" ^
    --min-sdk-version "%MIN_SDK%" ^
    --target-sdk-version "%TARGET_SDK%" ^
    --version-code "%VERSION_CODE%" ^
    --version-name "%VERSION_NAME%" ^
    -o knst_app-unsigned.apk
if errorlevel 1 (
    echo %R%[ERR ]%N%  aapt2 link failed
    popd
    exit /b 1
)

pushd apk
powershell -NoProfile -Command "Compress-Archive -Path 'lib\*' -DestinationPath '..\lib.zip' -Force" >nul 2>&1
popd
if exist lib.zip (
    REM Windows Compress-Archive produces .zip, but we need an APK-style .zip.
    REM Use native zip.exe if available, else PowerShell with System.IO.Compression
    del /q lib.zip
)
pushd apk
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::CreateFromDirectory('lib', '..\lib-part.zip')" >nul 2>&1
popd
if exist lib-part.zip (
    REM Merge lib-part.zip into knst_app-unsigned.apk (which already has manifest)
    powershell -NoProfile -Command "$dst=[IO.Compression.ZipFile]::OpenRead('knst_app-unsigned.apk'); $src=[IO.Compression.ZipFile]::OpenRead('lib-part.zip'); $out=[IO.Compression.ZipFile]::Open('knst_app-merged.apk','Create'); foreach($e in $dst.Entries){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($out,$e.FullName,$e.FullName)|Out-Null}; $dst.Dispose(); foreach($e in $src.Entries){$entry=$out.CreateEntry('lib/'+$e.FullName); $s=$e.Open(); $d=$entry.Open(); $s.CopyTo($d); $s.Dispose(); $d.Dispose()}; $src.Dispose(); $out.Dispose()" >nul 2>&1
    del /q knst_app-unsigned.apk
    move /y knst_app-merged.apk knst_app-unsigned.apk >nul
    del /q lib-part.zip
)
rmdir /s /q apk

"%BUILD_TOOLS%\zipalign.exe" -f 4 knst_app-unsigned.apk knst_app-aligned.apk
if errorlevel 1 (
    echo %R%[ERR ]%N%  zipalign failed
    popd
    exit /b 1
)

REM Keystore
if not exist "%USERPROFILE%\.android\debug.keystore" (
    echo %B%[INFO]%N%  Generating debug keystore
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
    echo %R%[ERR ]%N%  apksigner failed
    popd
    exit /b 1
)

del /q knst_app-unsigned.apk knst_app-aligned.apk 2>nul

for %%f in (knst_app.apk) do set "APK_SIZE=%%~zf"
echo         APK size     : %APK_SIZE% bytes
echo %G%[ OK ]%N%  APK created

popd

REM ============================================================================
REM  [7/8] DEPLOY
REM ============================================================================
echo.
echo %M%^>%N% %W%[7/8] Deploy%N%

adb -s "!ANDROID_SERIAL!" get-state 2>nul | findstr /r /c:"^device" >nul
if errorlevel 1 (
    echo %Y%[WARN]%N%  Device lost before install
    exit /b 1
)

echo %B%[INFO]%N%  Uninstalling previous version
adb -s "!ANDROID_SERIAL!" uninstall "%PACKAGE_NAME%" >nul 2>&1

echo %B%[INFO]%N%  Installing APK
adb -s "!ANDROID_SERIAL!" install -r "%BUILD_DIR%\knst_app.apk"
if errorlevel 1 (
    echo %R%[ERR ]%N%  Install failed
    exit /b 1
)

adb -s "!ANDROID_SERIAL!" logcat -c >nul 2>&1

echo %B%[INFO]%N%  Launching activity
adb -s "!ANDROID_SERIAL!" shell am start -n "%PACKAGE_NAME%/%ACTIVITY_NAME%" >nul
echo %G%[ OK ]%N%  Application launched

REM ============================================================================
REM  [8/8] LIVE LOGCAT
REM ============================================================================
echo.
echo %D%--------------------------------------------------------------%N%
echo   %G%v BUILD . PACKAGE . DEPLOY . LAUNCH - COMPLETE%N%
echo %D%--------------------------------------------------------------%N%
echo   %D%Device        :%N% !DEVICE_MODEL! (Android !DEVICE_ANDROID! / API !DEVICE_SDK!)
echo   %D%Serial        :%N% !ANDROID_SERIAL!
echo   %D%Source        :%N% %SOURCE_FILE%
echo   %D%ABI           :%N% !ABI!
echo   %D%SDK / NDK     :%N% %TARGET_SDK% / %NDK_VERSION%
echo   %D%APK           :%N% build-android\knst_app.apk
echo.
echo   %Y%Live logcat starting - press Ctrl+C to stop%N%
echo %D%--------------------------------------------------------------%N%
echo.

adb -s "!ANDROID_SERIAL!" logcat -v color KNST:* AndroidRuntime:E libc:E DEBUG:E *:S

endlocal