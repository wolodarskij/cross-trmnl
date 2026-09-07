@echo off
REM ---------------------------------------------------------------------------
REM install.bat - set up everything needed to build this project on Windows.
REM
REM   install.bat                  full setup (safe to re-run; every step is
REM                                idempotent)
REM   install.bat --skip-native    skip LLVM/CMake/Ninja - they are needed by
REM                                bin\clang-format-fix and the test\ gtest
REM                                suite, not by the firmware build
REM   install.bat --skip-toolchain skip the ESP32 toolchain pre-download; the
REM                                first build.bat will fetch it instead
REM   install.bat --with-msvc      install VS Build Tools if no MSVC C++
REM                                toolset is present (a multi-GB download,
REM                                so it is opt-in rather than automatic)
REM
REM What needs what, so the skips above are informed choices:
REM
REM   build.bat            -> PlatformIO + the ESP32 platform packages
REM   build_scripts.bat    -> Python, plus a host C compiler (MSVC or clang)
REM                           for tools\luac\build.py
REM   scripts\*.py, tools\gamedev\*.py
REM                        -> the packages in requirements.txt
REM   bin\clang-format-fix -> clang-format 21+ (ships with LLVM)
REM   test\CMakeLists.txt  -> CMake + Ninja + a compiler that takes GNU-style
REM                           flags (-Wall -Wextra -pedantic), i.e. clang/gcc
REM
REM Installing Python itself is left to you on purpose - except for the one
REM case in step 4 where the build cannot work without a second interpreter.
REM ---------------------------------------------------------------------------

setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

REM PlatformIO's console writer raises UnicodeEncodeError on the first
REM non-ASCII character it prints to a cp1252 console, which is the Windows
REM default and which reads exactly like a build failure. build.bat sets these
REM for the same reason.
set "PYTHONIOENCODING=utf-8"
set "PYTHONUTF8=1"

REM Cleared because the ESP32 platform installs its toolchain through
REM idf_tools.py, which aborts with "MSys/Mingw is not supported" the moment
REM it sees this variable. Launching install.bat from a Git Bash prompt sets
REM it, and cmd /c passes it straight through, so the guard belongs here and
REM not in the caller. Nothing this script runs is an MSYS program.
set "MSYSTEM="

REM pioarduino's PlatformIO fork, pinned to the version CI builds with
REM (.github/workflows/ci.yml) so a local build matches the one gating PRs.
set "PIO_PKG=https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip"

REM Expanded up here on purpose: %ProgramFiles(x86)% contains parentheses,
REM which the parser mis-reads inside an if/for block.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

set "SKIP_NATIVE="
set "SKIP_TOOLCHAIN="
set "WITH_MSVC="

:args
if "%~1"=="" goto start
if /i "%~1"=="--skip-native"    set "SKIP_NATIVE=1" & shift & goto args
if /i "%~1"=="--skip-toolchain" set "SKIP_TOOLCHAIN=1" & shift & goto args
if /i "%~1"=="--with-msvc"      set "WITH_MSVC=1" & shift & goto args
if /i "%~1"=="--help"           goto usage
if /i "%~1"=="-h"               goto usage
echo ERROR: unknown option "%~1"
echo.
goto usage

:usage
echo Usage: install.bat [--skip-native] [--skip-toolchain] [--with-msvc]
echo.
echo See the comment block at the top of this file for what each step
echo installs and which parts you actually need.
exit /b 2

:start
echo === [1/8] prerequisites ========================================

where python >nul 2>&1 || (
  echo ERROR: python not found on PATH.
  echo Install Python 3.8+ from https://www.python.org/downloads/ and tick
  echo "Add python.exe to PATH" in the installer.
  exit /b 9009
)
for /f "delims=" %%v in ('python --version 2^>^&1') do echo   %%v

where git >nul 2>&1 || (
  echo ERROR: git not found on PATH.
  echo Install it from https://git-scm.com/download/win
  exit /b 9009
)
for /f "delims=" %%v in ('git --version 2^>^&1') do echo   %%v

REM winget ships with Windows 11 and recent Windows 10. Without it the native
REM tools have to be installed by hand, but the firmware build still works,
REM so this is a warning and not a hard stop.
set "HAVE_WINGET=1"
where winget >nul 2>&1 || set "HAVE_WINGET="
if defined HAVE_WINGET (
  for /f "delims=" %%v in ('winget --version 2^>^&1') do echo   winget %%v
) else (
  echo   winget not found - steps 4 and 7 fall back to manual instructions
)

echo.
echo === [2/8] git submodules =======================================
REM freeink-sdk is a submodule and platformio.ini symlinks nine libraries out
REM of it, so an uninitialised submodule fails the build with errors that do
REM not mention submodules at all.
git submodule update --init --recursive || goto fail
git submodule status

echo.
echo === [3/8] git hooks ============================================
REM .githooks\pre-commit runs the formatter. Git ignores that directory until
REM core.hooksPath points at it, and the setting is per-clone.
git config core.hooksPath .githooks || goto fail
echo   core.hooksPath = .githooks

echo.
echo === [4/8] PlatformIO Core ======================================
REM PlatformIO must run on Python 3.10-3.13. The ESP32 platform hard-fails on
REM anything else, and on Windows specifically it excludes 3.14
REM (~\.platformio\platforms\espressif32\platform.py, "Python Version Check").
REM CI does not hit this because the same check permits 3.14 on Linux, so a
REM green CI run is no evidence that the local interpreter will do.
REM
REM Only PlatformIO is pinned this way. Everything under scripts\ and tools\
REM keeps running on whatever "python" resolves to, and the extra_scripts
REM PlatformIO itself executes import nothing outside the standard library.
set "PIO_PY="
call :pickpy "python"
if not defined PIO_PY call :pickpy "py -3.13"
if not defined PIO_PY call :pickpy "py -3.12"
if not defined PIO_PY call :pickpy "py -3.11"
if not defined PIO_PY call :pickpy "py -3.10"

if not defined PIO_PY (
  echo   No Python 3.10-3.13 found; PlatformIO cannot run on this machine yet.
  if not defined HAVE_WINGET (
    echo   ERROR: winget is unavailable, so it cannot be installed automatically.
    echo   Install Python 3.13 from https://www.python.org/downloads/ and re-run.
    goto fail
  )
  echo   Installing Python 3.13 alongside your current interpreter...
  winget install --id Python.Python.3.13 --exact --silent --accept-package-agreements --accept-source-agreements --disable-interactivity
  call :pickpy "py -3.13"
)
if not defined PIO_PY (
  echo   ERROR: still no usable Python 3.10-3.13 after installing.
  echo   Open a new terminal and re-run install.bat - the launcher may not
  echo   have picked up the new interpreter yet.
  goto fail
)
echo   PlatformIO will run on: %PIO_PY%
"%PIO_PY%" -m pip install -U "%PIO_PKG%" || goto fail

echo.
echo === [5/8] Python packages ======================================
REM requirements.txt pulls in scripts\ and lib\EpdFont\scripts\ transitively.
REM Installed into the default interpreter, because that is the one that runs
REM these scripts - build_scripts.bat and the tools\gamedev editors call
REM plain "python".
python -m pip install -r requirements.txt || goto fail

REM lupa is named separately because requirements.txt only covers scripts\ and
REM lib\EpdFont\scripts\, and this one belongs to tools\. Without it,
REM tools\luac\selftest.py fails its fourth check and takes build_scripts.bat
REM down with it at step 2 - the check needs a stock 64-bit Lua to dump a
REM chunk the device must reject, and lupa is where that Lua comes from.
python -m pip install lupa || goto fail

echo.
echo === [6/8] PATH =================================================
REM pip drops pio.exe beside the interpreter it installed into, which for a
REM per-user install is a directory Windows does not put on PATH. Find where
REM it actually landed and add that, otherwise build.bat cannot see it.
REM Via a temp file rather than a piped FOR /F. cmd mangles a FOR /F command
REM that starts with a quoted path - it swallows the closing quote and tries
REM to run 'C:\...\python.exe" -c "import' as one token.
set "PIOPATHS=%TEMP%\cp_pio_scripts.txt"
"%PIO_PY%" -c "import os,sysconfig; [print(d) for d in (sysconfig.get_path('scripts'), sysconfig.get_path('scripts','nt_user')) if os.path.exists(os.path.join(d,'pio.exe'))]" > "%PIOPATHS%" 2>nul
set "SCRIPTSDIR="
for /f "usebackq delims=" %%d in ("%PIOPATHS%") do (
  if not defined SCRIPTSDIR set "SCRIPTSDIR=%%d"
)
if exist "%PIOPATHS%" del "%PIOPATHS%" >nul 2>&1
if not defined SCRIPTSDIR (
  echo   WARNING: could not locate pio.exe after installing it.
  echo   Check the pip output above for errors.
) else (
  echo   pio.exe is in: !SCRIPTSDIR!
  call :addpath "!SCRIPTSDIR!"
)

echo.
echo === [7/8] native tools =========================================
if defined SKIP_NATIVE (
  echo   skipped ^(--skip-native^)
  goto after_native
)
if not defined HAVE_WINGET (
  echo   skipped - winget is not available.
  echo   Install by hand: LLVM ^(clang-format 21+^), CMake, Ninja.
  goto after_native
)

REM clang-format 21+ - .clang-format uses AlignFunctionDeclarations, which
REM older builds reject outright with "unknown key". LLVM also supplies the
REM clang that test\CMakeLists.txt needs for its GNU-style warning flags.
call :wg LLVM.LLVM "LLVM (clang-format)"
REM The LLVM installer does not put itself on PATH, and .githooks\pre-commit
REM runs bin\clang-format-fix, whose bash version resolves the binary purely
REM through "command -v" - no fallback to the install directory the PowerShell
REM version knows about. Leave PATH alone and every commit fails the hook.
if exist "%ProgramFiles%\LLVM\bin\clang-format.exe" call :addpath "%ProgramFiles%\LLVM\bin"
REM CMake and Ninja drive the host gtest suite under test\. Their installers
REM handle PATH themselves.
call :wg Kitware.CMake "CMake"
call :wg Ninja-build.Ninja "Ninja"

:after_native
echo.
echo   host C compiler ^(needed by tools\luac\build.py^):
REM Same discovery order tools\luac\build.py uses, so this reports what that
REM script will actually find rather than a guess.
set "HAVE_CC="
REM usebackq with backticks, so the quoted vswhere path survives the parser -
REM see the note in step 6 about FOR /F and leading quoted paths.
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%p in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "HAVE_CC=MSVC at %%p"
)
if not defined HAVE_CC (
  where clang >nul 2>&1 && set "HAVE_CC=clang on PATH"
)
if not defined HAVE_CC (
  where gcc >nul 2>&1 && set "HAVE_CC=gcc on PATH"
)

if defined HAVE_CC (
  echo     !HAVE_CC!
  goto toolchain
)
if not defined WITH_MSVC (
  echo     NONE FOUND - build_scripts.bat will fail at its first step.
  echo     Re-run with --with-msvc, or add the "Desktop development with C++"
  echo     workload from the Visual Studio installer.
  goto toolchain
)
if not defined HAVE_WINGET (
  echo     cannot install without winget - get VS Build Tools by hand.
  goto toolchain
)
echo     installing VS Build Tools - this is a multi-GB download...
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --silent --accept-package-agreements --accept-source-agreements --disable-interactivity --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
if errorlevel 1 echo     ^(winget exit %ERRORLEVEL%^)

:toolchain
echo.
echo === [8/8] ESP32 toolchain ======================================
if defined SKIP_TOOLCHAIN (
  echo   skipped ^(--skip-toolchain^) - build.bat will fetch it on first run
  goto verify
)
REM Prefer the pio just installed, by path rather than through PATH. A second
REM pio belonging to some other interpreter can easily sit earlier on PATH,
REM and picking that one up here would prime the wrong environment.
set "PIO="
if defined SCRIPTSDIR if exist "!SCRIPTSDIR!\pio.exe" set "PIO=!SCRIPTSDIR!\pio.exe"
if not defined PIO (
  REM Fall back to build.bat's own resolution order.
  set "PIO=pio"
  where pio >nul 2>&1 || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
)
if not "!PIO!"=="pio" if not exist "!PIO!" (
  echo   WARNING: PlatformIO not found; skipping the pre-download.
  goto verify
)
REM Fetches the compiler, framework and every lib_deps entry now, so the first
REM real build is a build and not a several-hundred-MB download.
"!PIO!" pkg install -e default || goto fail

:verify
echo.
echo === verification ===============================================
call :check python "python --version"
call :check git "git --version"
call :check pio "pio --version"
call :check cmake "cmake --version"
call :check ninja "ninja --version"
call :check clang-format "clang-format --version"
if defined HAVE_CC (
  echo   [ ok       ] host C compiler: !HAVE_CC!
) else (
  echo   [ missing  ] host C compiler
)

REM build.bat resolves pio through PATH, so if a different pio sits earlier
REM there, build.bat uses that one and not the one set up here. Worth naming,
REM because the resulting failure - the ESP32 platform's Python version check
REM - says nothing about PATH and reads like a broken install.
set "PIO_ONPATH="
for /f "usebackq delims=" %%q in (`where pio 2^>nul`) do (
  if not defined PIO_ONPATH set "PIO_ONPATH=%%q"
)
if defined PIO_ONPATH if defined SCRIPTSDIR (
  if /i not "!PIO_ONPATH!"=="!SCRIPTSDIR!\pio.exe" (
    echo.
    echo   WARNING: PATH resolves "pio" to
    echo     !PIO_ONPATH!
    echo   but this setup installed it at
    echo     !SCRIPTSDIR!\pio.exe
    echo   build.bat follows PATH, so remove or reorder the other one.
  )
)

echo.
echo Setup complete.
echo.
echo IMPORTANT: open a NEW terminal before building. PATH changes never reach
echo shells that were already running, this one included, so tools installed
echo just now can still show as "missing" above.
echo.
echo   build.bat                 build firmware -^> .pio\build\default\firmware.bin
echo   build.bat default upload  build, then flash over USB
echo   build_scripts.bat         build the SD card /scripts tree
echo   bin\clang-format-fix      format before committing
exit /b 0

REM --- helpers ---------------------------------------------------------------

REM %1 = a directory to put on the persistent user PATH, if not already there.
REM
REM Written straight to HKCU\Environment rather than through setx, which
REM truncates PATH at 1024 characters and would silently eat entries. Read
REM back unexpanded and rewritten as ExpandString so existing
REM %%USERPROFILE%%-style entries survive as variables instead of being frozen
REM to today's value. Compared with the trailing slash normalised, because
REM Python's own installer writes its entry with one and winget does not.
:addpath
set "CP_ADD=%~1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$k='HKCU:\Environment'; $raw=(Get-Item $k).GetValue('Path','',[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames); $add=$env:CP_ADD; $have=($raw -split ';' | ForEach-Object { $_.TrimEnd('\').ToLowerInvariant() }); if ($have -notcontains $add.TrimEnd('\').ToLowerInvariant()) { Set-ItemProperty -Path $k -Name Path -Value ($raw.TrimEnd(';')+';'+$add) -Type ExpandString; Write-Host ('  added to your user PATH: '+$add) } else { Write-Host ('  already on your user PATH: '+$add) }"
REM Also make it visible to the rest of THIS run; the registry write only
REM reaches shells started from now on.
set "PATH=%PATH%;%~1"
exit /b 0

REM %1 = a python invocation, e.g. "python" or "py -3.13". Sets PIO_PY to that
REM interpreter's real path if its version satisfies the ESP32 platform's
REM Windows range, and leaves PIO_PY alone otherwise. First caller to match
REM wins, so the default interpreter is preferred when it is usable.
:pickpy
if defined PIO_PY exit /b 0
%~1 -c "import sys; raise SystemExit(0 if (3,10) <= sys.version_info[:2] < (3,14) else 1)" >nul 2>&1
if errorlevel 1 exit /b 0
for /f "delims=" %%p in ('%~1 -c "import sys; print(sys.executable)" 2^>nul') do set "PIO_PY=%%p"
exit /b 0

REM winget exits non-zero when a package is already installed or already
REM current, which is not a failure here. Report it and carry on; the
REM verification block is what actually decides.
:wg
echo.
echo   -- %~2
winget install --id %~1 --exact --silent --accept-package-agreements --accept-source-agreements --disable-interactivity
if errorlevel 1 echo   ^(winget exit %ERRORLEVEL% - already installed or unchanged^)
exit /b 0

REM %1 = executable to look for, %2 = command printing its version
:check
where %~1 >nul 2>&1 || (
  echo   [ missing  ] %~1
  exit /b 0
)
for /f "delims=" %%v in ('%~2 2^>^&1') do (
  echo   [ ok       ] %%v
  exit /b 0
)
exit /b 0

:fail
set "RC=%ERRORLEVEL%"
echo.
echo INSTALL FAILED - exit code %RC%
exit /b %RC%
