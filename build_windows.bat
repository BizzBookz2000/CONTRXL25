@echo off
rem ==========================================================================
rem  CONTRXL 25 - one-click Windows build
rem  Works with Visual Studio 2019, 2022 and 2026 (any edition, or Build Tools)
rem  with the "Desktop development with C++" workload.
rem  It loads Visual Studio's own compiler environment and builds with Ninja,
rem  so it never depends on a particular Visual Studio version number.
rem  The first run downloads JUCE (about 70 MB) and takes 3-8 minutes.
rem  Result: dist\CONTRXL 25.exe
rem ==========================================================================
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
set "VSNOTE="

rem ---- find a Visual Studio (2019 / 2022 / 2026, any edition, prerelease too) that has the C++ x64 compiler
if exist "!VSWHERE!" (
    rem 1. the normal way: an installation that reports the C++ tools
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
    rem 2. also installations that are in the middle of an update / waiting for a restart
    if not defined VSPATH for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -all -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VSPATH=%%i"
        set "VSNOTE=1"
    )
    rem 3. any installation that simply has the compiler files
    if not defined VSPATH for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -all -prerelease -products * -property installationPath`) do if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%%i"
)
rem 4. no luck with the installer's list: look in the usual folders
if not defined VSPATH for /d %%v in ("%ProgramFiles%\Microsoft Visual Studio\*" "%ProgramFiles(x86)%\Microsoft Visual Studio\*") do for /d %%e in ("%%~v\*") do if exist "%%~e\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%%~e"

if not defined VSPATH (
    echo.
    echo  Could not find the Visual Studio C++ compiler.
    if exist "!VSWHERE!" (
        echo  Visual Studio installations found:
        "!VSWHERE!" -all -prerelease -products * -format text -property displayName
        "!VSWHERE!" -all -prerelease -products * -format text -property installationPath
        "!VSWHERE!" -all -prerelease -products * -format text -property isComplete
    )
    echo.
    echo  Fix: open "Visual Studio Installer". If it shows an update or "Restart required",
    echo  finish it and restart the PC. Otherwise click Modify and tick
    echo  "Desktop development with C++" - on the right keep "MSVC ... x64/x86 build tools"
    echo  and "C++ CMake tools for Windows" ticked - then run this file again.
    echo.
    pause
    exit /b 1
)
if defined VSNOTE (
    echo  Note: Visual Studio reports an unfinished update or a pending restart.
    echo  Trying anyway - if the build fails, finish the update in Visual Studio Installer.
)
echo Using Visual Studio at "%VSPATH%"

set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo  Could not find vcvars64.bat - repair the C++ workload in Visual Studio Installer.
    pause
    exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 (
    echo  Could not load the Visual Studio compiler environment.
    pause
    exit /b 1
)

rem ---- prefer the CMake and Ninja that ship with this Visual Studio (they always match it)
set "VSCMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "VSNINJA=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if exist "%VSCMAKE%\cmake.exe" set "PATH=%VSCMAKE%;%PATH%"
if exist "%VSNINJA%\ninja.exe" set "PATH=%VSNINJA%;%PATH%"

where cmake >nul 2>nul
if errorlevel 1 (
    echo.
    echo  CMake was not found. In Visual Studio Installer add the component
    echo  "C++ CMake tools for Windows", or install CMake from https://cmake.org/download/
    echo.
    pause
    exit /b 1
)
where ninja >nul 2>nul
if errorlevel 1 (
    echo.
    echo  Ninja was not found. In Visual Studio Installer add the component
    echo  "C++ CMake tools for Windows" - it includes Ninja.
    echo.
    pause
    exit /b 1
)
cmake --version | findstr /i "version"
where cl

rem ---- the Windows SDK (rc.exe, Windows headers and libraries) must be there too
where rc >nul 2>nul
if errorlevel 1 (
    echo.
    echo  The Windows SDK was not found - the C++ compiler is there, but rc.exe and the
    echo  Windows headers / libraries are missing, so nothing can be built yet.
    if defined VSNOTE echo  Visual Studio also reports an unfinished update: that is the most likely cause.
    echo.
    echo  Fix:
    echo    1. Open "Visual Studio Installer". If it shows Update, Resume or "Restart required",
    echo       finish it and restart the PC.
    echo    2. Then click Modify on Visual Studio 2026, open "Individual components", search for
    echo       "Windows 11 SDK", tick the newest one, and click Modify.
    echo    3. Delete the "build-ninja" folder and run this file again.
    echo.
    pause
    exit /b 1
)

rem ---- get JUCE ourselves (more reliable than letting CMake download it)
set "JUCEVER=8.0.12"
set "JUCEPATH=%~dp0JUCE"
if exist "%JUCEPATH%\CMakeLists.txt" goto :havejuce
rem a JUCE zip unzipped one level too deep: JUCE\JUCE-8.0.12\CMakeLists.txt
if exist "%~dp0JUCE\JUCE-%JUCEVER%\CMakeLists.txt" (
    set "JUCEPATH=%~dp0JUCE\JUCE-%JUCEVER%"
    goto :havejuce
)
rem a copy unpacked by an earlier run that stopped before renaming it
if exist "%~dp0JUCE-%JUCEVER%\CMakeLists.txt" goto :renamejuce
rem reuse a JUCE folder you already have (next to this folder, or from an earlier CONTRXL25 build)
for %%D in ("%~dp0..\JUCE" "%~dp0..\..\JUCE" "C:\dev\CONTRXL25\JUCE" "C:\dev\JUCE" "C:\JUCE" "%USERPROFILE%\JUCE") do if exist "%%~D\CMakeLists.txt" (
    set "JUCEPATH=%%~fD"
    goto :havejuce
)
echo.
echo Downloading JUCE %JUCEVER% (about 70 MB, one time only)...
set "JUCEURL=https://github.com/juce-framework/JUCE/archive/refs/tags/%JUCEVER%.zip"
if exist juce_download.zip del /q juce_download.zip
set /a TRY=0
:dlretry
set /a TRY+=1
rem -C - resumes a cut download instead of starting over
curl.exe -L --fail --retry 5 --retry-delay 3 --retry-all-errors --connect-timeout 30 -C - -o juce_download.zip "%JUCEURL%"
if not errorlevel 1 goto :dlok
if %TRY% LSS 3 (
    echo   Download interrupted - trying again, attempt %TRY% of 3...
    timeout /t 5 /nobreak >nul
    goto :dlretry
)
echo   curl could not finish - trying with PowerShell instead...
if exist juce_download.zip del /q juce_download.zip
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol='Tls12'; Invoke-WebRequest -UseBasicParsing -Uri '%JUCEURL%' -OutFile 'juce_download.zip'"
if errorlevel 1 goto :jucefail
:dlok
echo Unpacking JUCE...
if exist "JUCE-%JUCEVER%" rmdir /s /q "JUCE-%JUCEVER%"
tar -xf juce_download.zip
if errorlevel 1 goto :jucefail
if not exist "JUCE-%JUCEVER%\CMakeLists.txt" goto :jucefail
if exist juce_download.zip del /q juce_download.zip
:renamejuce
rem an old, empty or broken "JUCE" folder is in the way: move it aside
if exist "JUCE\" (
    echo   Moving an incomplete old JUCE folder out of the way...
    ren "JUCE" "JUCE_old_%RANDOM%"
)
ren "JUCE-%JUCEVER%" JUCE
if errorlevel 1 goto :jucefail
set "JUCEPATH=%~dp0JUCE"
:havejuce
echo Using JUCE in "%JUCEPATH%"

rem ---- configure + build (own folder, so an old failed "build" folder can't interfere)
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl "-DJUCE_DIR=%JUCEPATH%"
if errorlevel 1 goto :fail
cmake --build build-ninja --parallel
if errorlevel 1 goto :fail

if not exist dist mkdir dist
copy /y "build-ninja\CONTRXL25_artefacts\Release\CONTRXL 25.exe" "dist\" >nul
if errorlevel 1 goto :fail
rem the plug-in scanner helper must sit next to the .exe (it keeps broken plug-ins from crashing the app)
copy /y "build-ninja\CONTRXL25_artefacts\Release\CONTRXL25_PluginScanner.exe" "dist\" >nul
if errorlevel 1 goto :fail

echo.
echo  ==========================================================
echo   Done!  dist\CONTRXL 25.exe
echo   Keep "CONTRXL25_PluginScanner.exe" in the same folder as the app.
echo  ==========================================================
echo.
start "" "%~dp0dist"
pause
exit /b 0

:jucefail
echo.
echo  Could not download or unpack JUCE automatically.
echo  Do it by hand (2 minutes):
echo    1. Open https://github.com/juce-framework/JUCE/releases/tag/%JUCEVER%
echo    2. Under "Assets" download "Source code (zip)".
echo    3. Unzip it. You get a folder "JUCE-%JUCEVER%". Rename it to "JUCE".
echo    4. Move that "JUCE" folder into "%~dp0"
echo       so that "%~dp0JUCE\CMakeLists.txt" exists.
echo    5. Run build_windows.bat again.
echo  Tip: if an older CONTRXL25 folder already has a "JUCE" folder, just copy it here.
echo.
pause
exit /b 1

:fail
echo.
echo  The build stopped with an error. Scroll up to the first "error" line.
echo  If you change Visual Studio versions, delete the "build-ninja" folder and run again.
echo  Common fixes are listed in README.md (section "If something goes wrong").
echo.
pause
exit /b 1
