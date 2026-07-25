#!/usr/bin/env python3
"""Compile a /scripts tree to bytecode for the SD card.

Mirrors the layout of a source tree into an output tree: every `.lua` becomes a
`.luac`, everything else (images, data files) is copied as-is. The firmware's
`require` prefers `.luac` over `.lua` in the same directory, and the script
browser lists both, so the result drops straight onto a card.

The source tree is never written to. `lua-scripts-src/` is authored by hand and
is the single source of truth; the simulator runs it directly, because lupa's Lua
is a stock 64-bit build and physically cannot load the device's 32-bit bytecode.
Compiled output is a deployment artifact, verified here with `luac --check`
rather than by running it.

    python tools/luac/compile_tree.py                       # lua-scripts-src -> lua-scripts-build
    python tools/luac/compile_tree.py path/to/src -o out    # anywhere else
    python tools/luac/compile_tree.py --no-strip            # keep file:line in errors
"""

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
LUAC = os.path.join(HERE, "bin", "luac.exe" if os.name == "nt" else "luac")

# SDCardManager::kMaxReadFileBytes. Every whole-file read on the device lands in
# one contiguous Arduino String on a ~275 KB heap, so the firmware refuses
# anything larger — for bytecode exactly as for source.
MAX_READ_BYTES = 50000


def fmt_kb(n):
    return f"{n / 1024:.1f} KB"


def gather(src, overlay):
    """Relative path -> absolute source file, overlay files winning.

    The overlay is how generated artifacts (e.g. pack_dialogs.py output in
    lua-scripts-stage) replace their authored counterparts on the card without the
    authored tree ever being written to: dialogs.lua is shadowed by the packed
    core, dialogs.bin and the arc modules simply appear."""
    files = {}
    for root in (src, overlay) if overlay else (src,):
        for dirpath, _dirnames, filenames in os.walk(root):
            rel = os.path.relpath(dirpath, root)
            for name in sorted(filenames):
                key = os.path.join(rel, name) if rel != "." else name
                files[key] = os.path.join(dirpath, name)
    return files


def compile_tree(src, dst, strip, check, overlay=None):
    if os.path.abspath(src) == os.path.abspath(dst):
        sys.exit("refusing to compile a tree onto itself")
    if os.path.abspath(dst).startswith(os.path.abspath(src) + os.sep):
        sys.exit(f"refusing to write output inside the source tree ({src})")

    rows = []
    copied = 0
    failures = []

    for relpath, srcfile in sorted(gather(src, overlay).items()):
        name = os.path.basename(relpath)
        outdir = os.path.join(dst, os.path.dirname(relpath))
        os.makedirs(outdir, exist_ok=True)
        if not name.endswith(".lua"):
            shutil.copy2(srcfile, os.path.join(outdir, name))
            copied += 1
            continue
        outfile = os.path.join(outdir, name[: -len(".lua")] + ".luac")
        cmd = [LUAC]
        if strip:
            cmd.append("-s")
        cmd += ["-o", outfile, srcfile]
        if subprocess.run(cmd).returncode != 0:
            failures.append(srcfile)
            continue
        before = os.path.getsize(srcfile)
        after = os.path.getsize(outfile)
        rows.append((os.path.relpath(outfile, dst), before, after))
        # A file the device cannot read is not a build product, it is a
        # trap that only springs on hardware. Same wording the firmware
        # uses, so the two failures read alike.
        if after >= MAX_READ_BYTES:
            failures.append(
                f"{outfile} is {after} bytes; the SD reader stops at "
                f"{MAX_READ_BYTES}. Split it into require()d modules."
            )

    if check and rows and not failures:
        outputs = [os.path.join(dst, r[0]) for r in rows]
        if subprocess.run([LUAC, "--check"] + outputs,
                          stdout=subprocess.DEVNULL).returncode != 0:
            failures.append("one or more outputs failed --check")

    return rows, copied, failures


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", nargs="?", default=os.path.join(ROOT, "lua-scripts-src"),
                    help="source tree (default: lua-scripts-src)")
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "lua-scripts-build"),
                    help="output tree (default: lua-scripts-build)")
    ap.add_argument("--no-strip", action="store_true",
                    help="keep debug info: errors still report file:line, ~17 KB more heap on a large game")
    ap.add_argument("--no-check", action="store_true",
                    help="skip verifying that each output loads (not recommended)")
    ap.add_argument("--clean", action="store_true", help="remove the output tree first")
    ap.add_argument("--overlay", default=None, metavar="DIR",
                    help="tree of generated files laid over src: same relative path replaces "
                         "the authored file, extras are added (e.g. lua-scripts-stage from pack_dialogs.py)")
    args = ap.parse_args()

    if not os.path.exists(LUAC):
        sys.exit(f"{LUAC} not found — run: python tools/luac/build.py")
    if not os.path.isdir(args.src):
        sys.exit(f"no such directory: {args.src}")
    if args.overlay and not os.path.isdir(args.overlay):
        sys.exit(f"no such overlay directory: {args.overlay}")

    if args.clean and os.path.isdir(args.out):
        shutil.rmtree(args.out)

    rows, copied, failures = compile_tree(args.src, args.out, not args.no_strip, not args.no_check,
                                          overlay=args.overlay)

    width = max((len(r[0]) for r in rows), default=20)
    for name, before, after in sorted(rows):
        delta = (after - before) / before * 100 if before else 0
        print(f"  {name:<{width}}  {fmt_kb(before):>9} -> {fmt_kb(after):>9}  {delta:+5.0f}%")
    tb = sum(r[1] for r in rows)
    ta = sum(r[2] for r in rows)
    print(f"  {'':<{width}}  {fmt_kb(tb):>9} -> {fmt_kb(ta):>9}"
          f"  {((ta - tb) / tb * 100) if tb else 0:+5.0f}%   "
          f"{len(rows)} compiled, {copied} copied"
          f"{'' if args.no_strip else ', stripped'}")
    print(f"  -> {args.out}")
    # On-card size is not the win. Bytecode skips the parser at load, so the
    # peak a script reaches while loading drops well below its 1.28x parse peak,
    # and -s drops the debug arrays that survive the parse. Measure the real
    # number with device.mem() (meminfo.lua) on hardware.

    if failures:
        print("", file=sys.stderr)
        for f in failures:
            print(f"FAILED: {f}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
