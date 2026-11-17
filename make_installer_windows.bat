@echo off
rem ==========================================================================
rem  CONTRXL 25 - build the Windows setup program
rem  Result: installers\CONTRXL25-<version>-Windows-Setup.exe
rem
rem  1. Builds the app with build_windows.bat (skipped if dist\ is up to date;
rem     run "make_installer_windows.bat rebuild" to force a fresh build).
rem  2. Gets Inno Setup 6 (free) if it is not installed - one time, about 6 MB.
rem  3. Compiles the setup wizard from installer\windows\CONTRXL25.iss.
rem
rem  Optional code signing (removes the "Windows protected your PC" warning on
rem  other PCs): set CONTRXL_PFX=C:\path\cert.pfx and CONTRXL_PFX_PASS=password
rem  before running. Needs signtool.exe (Windows SDK, part of Visual Studio).
rem ==========================================================================
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

rem ---- version from CMakeLists.txt  (project(CONTRXL25 VERSION x.y.z ...))
set "APPVER="
for /f "tokens=3" %%v in ('findstr /r /c:"^project(CONTRXL25 VERSION" CMakeLists.txt') do set "APPVER=%%v"
if not defined APPVER (
    echo  Could not read the version from CMakeLists.txt.
    pause & exit /b 1
)
echo ==========================================================
echo   CONTRXL 25 %APPVER% - Windows setup builder
echo ==========================================================

rem ---- 1. the app
set "NEEDBUILD="
if /i "%~1"=="rebuild" set "NEEDBUILD=1"
if not exist "dist\CONTRXL 25.exe" set "NEEDBUILD=1"
if not exist "dist\CONTRXL25_PluginScanner.exe" set "NEEDBUILD=1"
if defined NEEDBUILD (
    echo.
    echo  Building the app first ^(build_windows.bat^)...
    set "CONTRXL_NOPAUSE=1"
    call build_windows.bat
    if errorlevel 1 ( echo  The app build failed - fix that first. & pause & exit /b 1 )
    set "CONTRXL_NOPAUSE="
)
if not exist "dist\CONTRXL 25.exe" ( echo  dist\CONTRXL 25.exe is missing. & pause & exit /b 1 )
echo  App: dist\CONTRXL 25.exe

rem ---- 2. Inno Setup
call :findiscc
if not defined ISCC (
    echo.
    echo  Inno Setup 6 is not installed. Getting it now ^(free, one time^)...
    where winget >nul 2>nul && winget install --id JRSoftware.InnoSetup -e --silent --accept-package-agreements --accept-source-agreements >nul 2>nul
    call :findiscc
)
if not defined ISCC (
    if not exist tools mkdir tools
    curl.exe -L --fail --retry 3 -o "tools\innosetup.exe" "https://jrsoftware.org/download.php/is.exe"
    if exist "tools\innosetup.exe" "tools\innosetup.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER
    call :findiscc
)
if not defined ISCC (
    echo.
    echo  Could not get Inno Setup automatically.
    echo  Download it from https://jrsoftware.org/isdl.php , install it, then run this file again.
    pause & exit /b 1
)
echo  Inno Setup: !ISCC!

rem ---- optional: sign the app before packing it
call :findsigntool
if defined CONTRXL_PFX if defined SIGNTOOL (
    echo  Signing the app...
    "!SIGNTOOL!" sign /f "%CONTRXL_PFX%" /p "%CONTRXL_PFX_PASS%" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 "dist\CONTRXL 25.exe" "dist\CONTRXL25_PluginScanner.exe"
    if errorlevel 1 ( echo  Signing failed. & pause & exit /b 1 )
)

rem ---- 3. compile the setup
if not exist installers mkdir installers
"!ISCC!" /Q /DAppVersion=%APPVER% "installer\windows\CONTRXL25.iss"
if errorlevel 1 ( echo. & echo  Inno Setup reported an error ^(see above^). & pause & exit /b 1 )
set "SETUP=installers\CONTRXL25-%APPVER%-Windows-Setup.exe"
if defined CONTRXL_PFX if defined SIGNTOOL (
    "!SIGNTOOL!" sign /f "%CONTRXL_PFX%" /p "%CONTRXL_PFX_PASS%" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 "%SETUP%"
)

echo.
echo  ==========================================================
echo   Done!  %SETUP%
echo   Double-click it to install CONTRXL 25 like any other program.
echo   Give that one file to anyone with 64-bit Windows 10 / 11.
echo  ==========================================================
start "" "%~dp0installers"
if not defined CONTRXL_NOPAUSE pause
exit /b 0

:findiscc
set "ISCC="
for %%p in ("%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe" "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe") do if exist "%%~p" set "ISCC=%%~p"
if not defined ISCC for /f "delims=" %%p in ('where ISCC.exe 2^>nul') do set "ISCC=%%p"
exit /b 0

:findsigntool
set "SIGNTOOL="
for /f "delims=" %%p in ('where signtool.exe 2^>nul') do set "SIGNTOOL=%%p"
if not defined SIGNTOOL for /d %%k in ("%ProgramFiles(x86)%\Windows Kits\10\bin\10.*") do if exist "%%~k\x64\signtool.exe" set "SIGNTOOL=%%~k\x64\signtool.exe"
exit /b 0
