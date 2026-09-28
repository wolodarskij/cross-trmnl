@echo off
REM ---------------------------------------------------------------------------
REM build.bat - build the firmware with PlatformIO.
REM
REM   build.bat                  build the "default" env
REM   build.bat slim             build another env from platformio.ini
REM   build.bat default upload   build, then flash over USB
REM   build.bat default clean    remove that env's build artifacts
REM   build.bat default -v       flags are passed through to pio
REM
REM Optional features (all on by default; any position on the command line):
REM   --no-bluetooth   BLE keyboards / page-turner remotes (NimBLE)
REM   --no-dashboard   x4-dashboard-server dashboard source
REM   --no-trmnl       TRMNL / BYOS dashboard source
REM   --no-lua         Lua script runner and the Scripts menu
REM   --no-tasks       Task lists, their pages and the Tasks menu
REM   e.g.  build.bat default --no-lua --no-trmnl
REM They are exported as CROSSPOINT_FEATURE_<NAME>=0 for scripts/features.py.
REM
REM Envs in platformio.ini: default, gh_release, gh_release_rc, slim.
REM
REM This wrapper exists for one reason. PlatformIO's console writer raises
REM UnicodeEncodeError the moment it prints a non-ASCII character to a cp1252
REM console, which is the Windows default here. That aborts the run and reads
REM exactly like a compile failure while the code is in fact fine. Forcing
REM Python to UTF-8 avoids it.
REM ---------------------------------------------------------------------------

setlocal EnableExtensions
cd /d "%~dp0"

set "PYTHONIOENCODING=utf-8"
set "PYTHONUTF8=1"
REM ESP-IDF's idf_tools.py (run by the Arduino core rebuild that custom_sdkconfig
REM triggers) aborts with "MSys/Mingw is not supported" when it sees MSYSTEM in
REM the environment, i.e. whenever this script is launched from Git Bash. The
REM build itself is a plain Windows toolchain, so just hide the variable.
set "MSYSTEM="
set "MSYSTEM_PREFIX="

REM Feature switches: start from everything on, so a stale value in the caller's
REM environment cannot silently drop a feature.
set "CROSSPOINT_FEATURE_BLUETOOTH=1"
set "CROSSPOINT_FEATURE_DASHBOARD=1"
set "CROSSPOINT_FEATURE_TRMNL=1"
set "CROSSPOINT_FEATURE_LUA=1"
set "CROSSPOINT_FEATURE_TASKS=1"

REM First word is the env name.
set "ENVNAME=%~1"
if "%ENVNAME%"=="" set "ENVNAME=default"
if not "%~1"=="" shift

REM An optional second word that is not a flag is a pio target (upload, clean,
REM monitor, uploadfs, ...). Anything else is passed through untouched.
set "TARGET="
if "%~1"=="" goto collect
set "FIRST=%~1"
if "%FIRST:~0,1%"=="-" goto collect
set "TARGET=-t %FIRST%"
shift

:collect
set "PASS="
:collectloop
if "%~1"=="" goto resolve
if /I "%~1"=="--no-bluetooth" set "CROSSPOINT_FEATURE_BLUETOOTH=0" & shift & goto collectloop
if /I "%~1"=="--no-dashboard" set "CROSSPOINT_FEATURE_DASHBOARD=0" & shift & goto collectloop
if /I "%~1"=="--no-trmnl"     set "CROSSPOINT_FEATURE_TRMNL=0" & shift & goto collectloop
if /I "%~1"=="--no-lua"       set "CROSSPOINT_FEATURE_LUA=0" & shift & goto collectloop
if /I "%~1"=="--no-tasks"     set "CROSSPOINT_FEATURE_TASKS=0" & shift & goto collectloop
set "PASS=%PASS% %1"
shift
goto collectloop

:resolve
REM Prefer pio on PATH, else the stock PlatformIO install location.
set "PIO=pio"
where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
if not "%PIO%"=="pio" if not exist "%PIO%" (
  echo ERROR: PlatformIO not found on PATH or at "%PIO%".
  echo Install it with:  pip install platformio
  exit /b 9009
)

echo Building env:%ENVNAME% %TARGET%%PASS%
echo Features: bluetooth=%CROSSPOINT_FEATURE_BLUETOOTH% dashboard=%CROSSPOINT_FEATURE_DASHBOARD% trmnl=%CROSSPOINT_FEATURE_TRMNL% lua=%CROSSPOINT_FEATURE_LUA% tasks=%CROSSPOINT_FEATURE_TASKS%  (1 = on)
echo.
"%PIO%" run -e "%ENVNAME%" %TARGET%%PASS%
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
  echo BUILD FAILED - exit code %RC%
  exit /b %RC%
)
if "%TARGET%"=="" echo Build OK.  .pio\build\%ENVNAME%\firmware.bin
if not "%TARGET%"=="" echo Done.
exit /b 0
