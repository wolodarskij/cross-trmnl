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
