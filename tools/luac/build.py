#!/usr/bin/env python3
"""Build the host `luac` from the firmware's own vendored Lua.

Why not a stock luac: lundump.c's checkHeader compares sizeof(Instruction),
sizeof(lua_Integer) and sizeof(lua_Number) against the loading VM, and
lib/Lua/luaconf.h hard-codes LUA_32BITS=1. Compiling these exact sources gives
the host tool the device's numeric config *by construction* — there is no
-DLUA_32BITS to pass, and therefore none to forget or get wrong. Pointer and
size_t width are not part of the header check, so a 64-bit host is fine.

Compiler discovery, in order: cc/gcc/clang on PATH, then MSVC located through
vswhere. Nothing is downloaded or installed.

    python tools/luac/build.py [--rebuild]
"""

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
LUA_DIR = os.path.join(ROOT, "lib", "Lua")
BIN_DIR = os.path.join(HERE, "bin")
OBJ_DIR = os.path.join(BIN_DIR, "obj")
EXE = os.path.join(BIN_DIR, "luac.exe" if os.name == "nt" else "luac")


def lua_sources():
    return sorted(
        os.path.join(LUA_DIR, f) for f in os.listdir(LUA_DIR) if f.endswith(".c")
    )


def find_unix_cc():
    for name in ("cc", "gcc", "clang"):
        path = shutil.which(name)
        if path:
            return path
    return None


def find_vcvarsall():
    """Locate MSVC's environment script via vswhere, the only supported way.

    Hard-coding a Visual Studio path breaks on the next machine; vswhere ships
    with every VS installer since 2017 and answers for whatever is installed.
    """
    program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = os.path.join(program_files_x86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.exists(vswhere):
        return None
    try:
        out = subprocess.run(
            [vswhere, "-latest", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"],
            capture_output=True, text=True, check=True,
        ).stdout.strip().splitlines()
    except (subprocess.CalledProcessError, OSError):
        return None
    if not out:
        return None
    candidate = os.path.join(out[0], "VC", "Auxiliary", "Build", "vcvarsall.bat")
    return candidate if os.path.exists(candidate) else None


def build_unix(cc, sources):
    cmd = [cc, "-O2", "-std=c99", "-o", EXE, "-I", LUA_DIR, "-I", HERE]
    cmd += sources
    cmd += ["-lm"]
    print(" ".join(cmd))
    return subprocess.run(cmd).returncode


def build_msvc(vcvarsall, sources):
    os.makedirs(OBJ_DIR, exist_ok=True)

    def q(path, trailing_slash=False):
        """Quote a path for cl, in forward slashes.

        Not cosmetic: cl needs a trailing separator on /Fo to mean "directory",
        and a trailing backslash immediately before a closing double quote
        escapes that quote, so `/Fo:"C:\\dir\\"` swallows the rest of the
        command line. Forward slashes have no such rule and cl accepts them.
        """
        p = path.replace("\\", "/")
        if trailing_slash and not p.endswith("/"):
            p += "/"
        return f'"{p}"'

    # /D_CRT_SECURE_NO_WARNINGS: Lua uses fopen/getenv throughout and MSVC's
    # "unsafe" warnings would bury a real one.
    cl = " ".join([
        "cl", "/nologo", "/O2", "/W3", "/D_CRT_SECURE_NO_WARNINGS",
        f"/I{q(LUA_DIR)}", f"/I{q(HERE)}",
        f"/Fo:{q(OBJ_DIR, trailing_slash=True)}", f"/Fe:{q(EXE)}",
    ] + [q(s) for s in sources])
    # Via a batch file rather than `cmd /c "<string>"`: the command contains
    # quoted paths with spaces, and cmd's own quote-stripping rules mangle them
    # differently depending on how Python escapes the argument. A file has no
    # such rules, and it is there to read when a build goes wrong.
    script_path = os.path.join(BIN_DIR, "_build_luac.bat")
    with open(script_path, "w", encoding="ascii") as f:
        f.write("@echo off\n")
        f.write(f'call "{vcvarsall}" x64 >nul || exit /b 1\n')
        f.write(cl + "\n")
    print(f"(vcvarsall x64) cl ... -> {EXE}")
    return subprocess.run(["cmd", "/c", script_path]).returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rebuild", action="store_true", help="rebuild even if luac is newer than the sources")
    args = ap.parse_args()

    sources = lua_sources() + [os.path.join(HERE, "luac.c")]
    if not sources:
        sys.exit(f"no Lua sources under {LUA_DIR}")

    if not args.rebuild and os.path.exists(EXE):
        newest = max(os.path.getmtime(s) for s in sources)
        if os.path.getmtime(EXE) >= newest:
            print(f"up to date: {EXE}")
            return 0

    os.makedirs(BIN_DIR, exist_ok=True)

    cc = find_unix_cc()
    if cc:
        rc = build_unix(cc, sources)
    else:
        vcvarsall = find_vcvarsall()
        if not vcvarsall:
            sys.exit(
                "no host C compiler found.\n"
                "  Looked for cc/gcc/clang on PATH, then MSVC via vswhere.\n"
                "  Install either: MSVC Build Tools (the 'Desktop development with C++'\n"
                "  workload) on Windows, or gcc/clang elsewhere. Nothing else is needed —\n"
                "  the Lua sources are already in lib/Lua."
            )
        rc = build_msvc(vcvarsall, sources)

    if rc != 0:
        sys.exit(f"compiler exited {rc}")

    print(f"built {EXE}")
    subprocess.run([EXE, "--info"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
