@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: KNST Android Runner
::
:: Usage:
::
::   run_android.bat
::
::   run_android.bat "<device>"
::
::   run_android.bat "<device>" "<apk>"
::
:: Examples:
::
::   run_android.bat
::
::   run_android.bat "adb-6bf73a6e-7SZbav (2)._adb-tls-connect._tcp"
::
::   run_android.bat "192.168.1.111:41883"
::
:: ============================================================


:: Configuration

set "ADB=%ADB%"
if "%ADB%"=="" set "ADB=adb"

set "PACKAGE=com.knst.test"
set "ACTIVITY=android.app.NativeActivity"


:: Script / Project Paths

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "PROJECT_ROOT=%SCRIPT_DIR%\..\..\.."
set "DEFAULT_APK=%PROJECT_ROOT%\build-android\knst_app.apk"


:: Arguments

set "DEVICE=%~1"
set "APK=%~2"
if "%APK%"=="" set "APK=%DEFAULT_APK%"


:: Header

echo.
echo ========================================
echo  KNST Android Runner
echo ========================================
echo Device:  %DEVICE%
echo Package: %PACKAGE%
echo Activity: %ACTIVITY%
echo APK:     %APK%
echo.


:: Check ADB

where %ADB% >nul 2>&1
if errorlevel 1 (
    echo ERROR: adb bulunamadi.
    echo.
    echo ADB'nin PATH icerisinde oldugundan emin ol.
    exit /b 1
)


:: Find Device Automatically

if "%DEVICE%"=="" (
    for /f "delims=" %%i in ('%ADB% devices ^| findstr /r "device$" ^| findstr /v "List"') do (
        if not defined DEVICE set "DEVICE=%%i"
    )
    if not "%DEVICE%"=="" set "DEVICE=!DEVICE:	 =!"
)


:: No Device

if "%DEVICE%"=="" (
    echo ERROR: Bagli Android cihazi bulunamadi.
    echo.
    echo Bagli cihazlar:
    echo.
    %ADB% devices
    echo.
    echo Ornek kullanim:
    echo.
    echo run_android.bat "192.168.1.111:41883"
    echo.
    echo run_android.bat "adb-xxxx (2)._adb-tls-connect._tcp"
    echo.
    exit /b 1
)


:: Check APK

if not exist "%APK%" (
    echo ERROR: APK bulunamadi:
    echo %APK%
    echo.
    echo Once build_android.bat calistir.
    exit /b 1
)


:: Check Current ADB State

%ADB% devices | findstr /c:"%DEVICE%" | findstr /r "device$" >nul
if not errorlevel 1 (
    echo Device already connected.
) else (
    echo Device is not currently connected.
    
    :: IP:PORT device
    echo %DEVICE% | findstr /r ":" >nul
    if not errorlevel 1 (
        echo.
        echo Connecting to:
        echo   %DEVICE%
        echo.
        %ADB% connect %DEVICE%
        timeout /t 1 /nobreak >nul
    ) else (
        echo.
        echo ERROR: Cihaz ADB tarafindan bagli gorunmuyor:
        echo %DEVICE%
        echo.
        echo ADB devices:
        echo.
        %ADB% devices
        exit /b 1
    )
)


:: Verify Device

%ADB% devices | findstr /c:"%DEVICE%" | findstr /r "device$" >nul
if errorlevel 1 (
    echo.
    echo ERROR: ADB cihazi hazir degil.
    echo.
    echo Device: %DEVICE%
    echo.
    echo ADB devices:
    echo.
    %ADB% devices
    echo.
    exit /b 1
)

echo.
echo Device connected successfully.


:: Install APK

echo.
echo ========================================
echo  Installing APK
echo ========================================
echo.

%ADB% -s %DEVICE% install -r "%APK%"


:: Clear Old Logs

echo.
echo Clearing old logcat...

%ADB% -s %DEVICE% logcat -c


:: Start Application

echo.
echo ========================================
echo  Starting KNST
echo ========================================
echo.

%ADB% -s %DEVICE% shell am start -n %PACKAGE%/%ACTIVITY%

echo.
echo Application started.
echo.


:: Logcat

echo ========================================
echo  KNST LOGCAT
echo ========================================
echo.
echo Press Ctrl+C to stop logcat.
echo.

%ADB% -s %DEVICE% logcat -v color -s KNST:* AndroidRuntime:E DEBUG:E libc:E

endlocal