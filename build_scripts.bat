@echo off
REM ---------------------------------------------------------------------------
REM build_scripts.bat - build the distributable /scripts tree for the SD card.
REM
REM   build_scripts.bat              full pipeline -> lua-scripts-build
REM   build_scripts.bat --no-strip   keep file:line in runtime errors
REM                                  (any other flags pass through to
REM                                   compile_tree.py, e.g. --no-check)
REM
REM Runs, in order — each step feeds the next, so the order is the point:
REM
REM   1. tools\luac\build.py         host compiler from lib\Lua (no-op when
REM                                  up to date; rebuilds if lib\Lua changed)
REM   2. tools\luac\selftest.py      proves the compiler's output is bytecode
REM                                  the device will accept before any is made
REM   3. tools\gamedev\pack_dialogs.py
REM                                  adventure's story: prose -> dialogs.bin
REM                                  (streamed at play time via fs.readRange),
REM                                  lazy arc modules for fight/coda/deeds.
REM                                  Reads lua-scripts-src\, writes
REM                                  the authored tree is never touched.
REM                                  lua-scripts-stage\ (the authored tree is
REM                                  never touched).
REM   4. tools\luac\compile_tree.py --clean --overlay lua-scripts-stage
REM                                  lua-scripts-src + the packed overlay,
REM                                  compiled and stripped -> lua-scripts-build
REM
REM Then copy lua-scripts-build onto the SD card as /scripts. The card carries
REM .luac + .bin only; sources stay in the repo, where the simulator and the
REM editors keep working on them.
REM ---------------------------------------------------------------------------

setlocal EnableExtensions
cd /d "%~dp0"

set "PYTHONIOENCODING=utf-8"
set "PYTHONUTF8=1"

where python >nul 2>&1 || (
  echo ERROR: python not found on PATH.
  exit /b 9009
)

echo === [1/4] host luac (lib\Lua) ==================================
python tools\luac\build.py || goto fail

echo.
echo === [2/4] compiler selftest ====================================
python tools\luac\selftest.py || goto fail

echo.
echo === [3/4] pack adventure story =================================
REM Fresh stage: a renamed or removed arc must not leave a stale module
REM behind to be swept onto the card by the overlay.
if exist "lua-scripts-stage" rmdir /s /q "lua-scripts-stage"
REM Arc order matters: the first matching arc claims a node, so the specific
REM fight splinters must come before the fight_ catch-all. The round nodes
REM stay one arc for all loadouts on purpose — their choices are gated by
REM `when = {sword = ...}`, and splitting them would mean duplicating story.
python tools\gamedev\pack_dialogs.py lua-scripts-src\adventure\dialogs.lua ^
  -o lua-scripts-stage\adventure --require-prefix adventure ^
  --arc mushroom:fight_mushroom,fight_mushroom_eaten ^
  --arc die_armed:fight_die_slash_chomp,fight_die_pierce_stomp ^
  --arc die_bare:fight_die_fists_ ^
  --arc fight:fight_ ^
  --arc coda:storm,coda_,ghost,rest_,dragon_eats_,dragon_slay_scene ^
  --arc deeds:eat_mushroom,contemplate,drink_elixir,swing_sword,count_gold,uncork_ ^
  || goto fail

echo.
echo === [4/4] compile + strip card tree ============================
python tools\luac\compile_tree.py --clean --overlay lua-scripts-stage %* || goto fail

echo.
echo Scripts OK.  Copy lua-scripts-build to the SD card as /scripts
exit /b 0

:fail
set "RC=%ERRORLEVEL%"
echo.
echo SCRIPT BUILD FAILED - exit code %RC%
exit /b %RC%
