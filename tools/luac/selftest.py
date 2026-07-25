#!/usr/bin/env python3
"""Prove the compiled bytecode is the bytecode the device accepts.

Four checks, each of which has failed at some point in someone's project:

  1. this build's numeric config is the device's (32-bit ints, float numbers)
  2. a compiled script round-trips: `--check` loads back what `-o` wrote
  3. source is rejected by `--check` — otherwise a tree that was never
     compiled would pass the build's own verification
  4. a *stock* 64-bit chunk is rejected, naming the mismatch. This is the one
     that matters: it is the reason a system luac cannot be used here, and it
     runs against the Lua that lupa already ships, so it needs no second
     toolchain to demonstrate.

    python tools/luac/selftest.py
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
LUAC = os.path.join(HERE, "bin", "luac.exe" if os.name == "nt" else "luac")

failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'}  {name}{('  — ' + detail) if detail else ''}")
    if not ok:
        failures.append(name)


def run(*args):
    return subprocess.run([LUAC, *args], capture_output=True, text=True)


def main():
    if not os.path.exists(LUAC):
        sys.exit(f"{LUAC} not found — run: python tools/luac/build.py")

    tmp = tempfile.mkdtemp(prefix="luac-selftest-")
    src = os.path.join(ROOT, "lua-scripts-src", "hello.lua")
    out = os.path.join(tmp, "hello.luac")

    r = run("--info")
    check("numeric config matches the device",
          "lua_Integer  4 bytes" in r.stdout and "lua_Number   4 bytes" in r.stdout,
          r.stdout.splitlines()[0] if r.stdout else r.stderr.strip())

    r = run("-s", "-o", out, src)
    check("compile hello.lua", r.returncode == 0, r.stderr.strip())

    if os.path.exists(out):
        r = run("--check", out)
        check("compiled output loads back", r.returncode == 0, r.stderr.strip())

    r = run("--check", src)
    check("source is rejected as bytecode", r.returncode != 0, r.stderr.strip().split(": ")[-1])

    # The stock-config chunk, dumped by lupa's own 64-bit Lua 5.4.
    stock = os.path.join(tmp, "stock64.luac")
    try:
        from lupa import lua54
    except ImportError:
        check("stock 64-bit chunk is rejected", False, "lupa not installed (pip install lupa)")
    else:
        lua = lua54.LuaRuntime(encoding=None)  # binary-safe: string.dump returns bytes
        dump = lua.eval("function(s) return string.dump(load(s), true) end")
        data = dump(b"local x = 1 return x")
        with open(stock, "wb") as f:
            f.write(data)
        r = run("--check", stock)
        # Header layout: signature(4) version(1) format(1) LUAC_DATA(6), then
        # sizeof(Instruction), sizeof(lua_Integer), sizeof(lua_Number).
        widths = f"Instruction={data[12]} lua_Integer={data[13]} lua_Number={data[14]}"
        check("stock 64-bit chunk is rejected", r.returncode != 0,
              f"{widths}; {r.stderr.strip().split(': ')[-1]}")

    print()
    if failures:
        print(f"{len(failures)} check(s) failed", file=sys.stderr)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
