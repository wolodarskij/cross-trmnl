#!/usr/bin/env python3
"""Host-side simulator for cross-trmnl Lua scripts (docs/SCRIPTING.md API).

Runs a script against a mock of the device API so it can be tested on a PC
before copying it to the SD card. Two modes:

  Interactive (default) — opens a window showing the e-ink framebuffer.
      Keyboard: arrows = arrows, Enter/Space = confirm, P = power,
      Esc = soft back (returned to Lua as "back"); hold Esc ~0.8s force-quits
      (same as device long-hold Back). device.exit() also ends the script.

      python sim.py ../../lua-scripts-src/life.lua

  Headless — feeds a scripted key sequence, dumps every screen.update()
      as a PNG. For automated testing.

      python sim.py ../../lua-scripts-src/paint.lua \
          --keys confirm,right,confirm --frames out/

Key sequence tokens: confirm, up, down, left, right, power, back,
  backhold (force-quit like a long-hold Back), nil:N (N input.poll()
  calls return nil), then end-of-queue aborts. Requires: pip install lupa pillow

Approximations: fonts are host TTFs (not the firmware fonts), refresh modes
all render instantly, and the ~96 KB Lua memory cap is opt-in via --max-mem.
The device's 50 KB per-file SD read cap is always enforced, so an oversized
script or module fails here rather than only on hardware.
"""

import argparse
import os
import queue
import sys
import threading
import time
import urllib.request

from PIL import Image, ImageDraw, ImageFont

try:
    from lupa import lua54
except ImportError:
    sys.exit("lupa is required: pip install lupa")

BLACK, WHITE = 0, 255
FONT_SIZES = {"small": 16, "s": 16, "medium": 22, "large": 28, "l": 28, "xl": 28}
BUTTONS = {"confirm", "left", "right", "up", "down", "power", "back"}

# The firmware's four ink levels (GfxRenderer.h `enum Color`). The panel buffer
# is 1bpp, so the two grays are ordered dither, and the pattern is keyed on
# absolute screen coordinates — adjacent fills tile seamlessly, but a shape's
# appearance depends on where it lands. These predicates must stay identical to
# GfxRenderer.cpp drawPixelDither<LightGray>/<DarkGray> or the simulator will
# greenlight a design that looks wrong on hardware.
INK_LEVELS = {
    "black": "black",
    "darkgray": "darkgray",
    "darkgrey": "darkgray",
    "lightgray": "lightgray",
    "lightgrey": "lightgray",
    "white": "white",
}


def ink_inks(level, x, y):
    """Does `level` put black at absolute pixel (x, y)?"""
    if level == "black":
        return True
    if level == "white":
        return False
    if level == "lightgray":
        return x % 2 == 0 and y % 2 == 0
    return (x + y) % 2 == 0  # darkgray

# Known device panels, portrait logical coordinates (matches the firmware's
# device table). "x4" is the default target.
DEVICES = {"x4": (480, 800), "x3": (528, 792)}


def parse_device(spec):
    """'x4' / 'x3' / '528x792' → (width, height)."""
    if spec in DEVICES:
        return DEVICES[spec]
    try:
        w, h = spec.lower().split("x")
        return int(w), int(h)
    except ValueError:
        raise SystemExit(f"--device must be one of {', '.join(DEVICES)} or WxH, not {spec!r}")
# Match ScriptEngine::kBackHoldExitMs — long-hold Back always force-quits.
BACK_HOLD_EXIT_S = 0.8

# ---- memory model -----------------------------------------------------------
# The device's cap (src/scripting/ScriptEngine.h) counts every byte Lua asks its
# allocator for; lupa's counting allocator counts the same quantity, so the two
# are directly comparable once converted between word sizes.
#
# The device builds Lua with LUA_32BITS (lib/Lua/luaconf.h) — 32-bit integers
# AND 32-bit floats on 32-bit pointers — so TValue is 8 bytes where the 64-bit
# host uses 16, Node 16 vs 24, Table 32 vs 56, TString header 16 vs 24, Proto
# 84 vs 128. But bytecode (4 bytes/instruction), line info (1 byte/instruction)
# and string payloads are byte-identical on both targets, and for these scripts
# that incompressible part is ~28% of the heap. The whole-heap ratio therefore
# lands at 0.68, NOT the 0.5 that a naive "64-bit is twice 32-bit" would give.
#
# Verified by cross-compiling lib/Lua with the project's riscv32-esp-elf
# toolchain and costing the live heap object-by-object under both size tables.
# Array-heavy scripts (a big TValue array part does halve) run nearer 0.57.
DEVICE_RATIO = 0.68
DEVICE_CAP = 96 * 1024          # ScriptEngine.h memLimit_
# Host bytes equivalent to the device cap. Calibrated three ways — direct
# bisection of a device-modelled workload sitting exactly on the cap, and
# sim_min * cap / device_min for both shipped games (adventure 134469,
# catgo 132120). The old documented 196608 admits a game ~35% over the cap.
HOST_BUDGET = int(DEVICE_CAP / DEVICE_RATIO)


def fmt_kb(n):
    return f"{n / 1024:.1f} KB"


# ---- SD whole-file read cap --------------------------------------------------
# HalStorage::kMaxReadFileBytes. Every device-side whole-file read — the main
# script, each require()d module, fs.read, and the read half of fs.append —
# lands in one contiguous Arduino String on the general ESP32 heap, so the
# firmware caps it and refuses anything larger. Mirrored here because a
# simulator that reads whole files regardless will greenlight an oversized
# module that then fails only on hardware, which is the worst place to find it.
MAX_READ_BYTES = 50000

# Every whole-file read this run made: [(path, bytes)], for the memory report.
SOURCE_READS = []


def too_large(path, size):
    return (f"{path} is {size} bytes; the SD reader stops at {MAX_READ_BYTES}. "
            f"Split it into require()d modules.")


# ---- precompiled chunks ------------------------------------------------------
# The device loads .luac; this simulator cannot, and never will. lupa embeds a
# stock 64-bit Lua 5.4 (lua_Integer 8, lua_Number double) while the firmware is
# built with LUA_32BITS, and lundump.c's checkHeader rejects across that
# boundary in both directions. Running the compiled tree here would require
# shipping a second Lua, or compiling every script twice into two artifacts that
# are supposed to be the same script.
#
# So source stays the single truth: the simulator runs .lua, and .luac is a
# deployment artifact verified by `tools/luac/bin/luac --check`. What the
# simulator owes you is a clear sentence when you point it at the wrong tree —
# not a UnicodeDecodeError naming a codec.
LUA_SIGNATURE = b"\x1bLua"


def read_source(path, what):
    """Read a Lua source file, refusing bytecode with an explanation."""
    with open(path, "rb") as f:
        raw = f.read()
    if raw.startswith(LUA_SIGNATURE) or path.endswith(".luac"):
        raise RuntimeError(
            f"{what} is precompiled bytecode ({path}).\n"
            f"  The simulator runs from source — lupa's Lua is a 64-bit build and "
            f"cannot load the device's 32-bit chunks.\n"
            f"  Point it at the .lua tree, and verify the compiled one with:\n"
            f"    tools/luac/bin/luac --check {path}")
    return raw.decode("utf-8")


def check_read_size(path, size, prefix=""):
    """Record a whole-file read and reject it if the device could not do it.

    `prefix` names the API that asked, matching the firmware's wording
    (ScriptBindings.cpp / ScriptEngine.cpp) so the same failure reads the same
    on both sides."""
    SOURCE_READS.append((path, size))
    if size > MAX_READ_BYTES:
        raise RuntimeError(f"{prefix}: {too_large(path, size)}" if prefix
                           else too_large(path, size))


class MemoryMeter:
    """Tracks Lua heap use for a run: current, peak, and per-module cost.

    `get_memory_used()` and collectgarbage("count") are both strictly *current*
    live bytes, and a script's peak runs well above its steady state (~1.7x for
    adventure.lua), so peak needs its own sampler: a Lua count hook, which fires
    every N VM instructions and costs no measurable wall-clock.

    Two honest limits, neither fixable from here. The hook cannot fire during
    chunk *compilation* — the parser builds the whole Proto tree before a single
    VM instruction runs — so a module whose peak is its own parse is sampled
    only once it starts executing. And Lua 5.4 hooks are per-thread, so
    allocations inside a coroutine go unsampled.
    """

    HOOK_EVERY = 200  # VM instructions between samples

    def __init__(self, runtime):
        self.rt = runtime
        self.peak = 0
        self.modules = []          # [(depth, name, bytes)] in load order
        self._stack = []
        self.enabled = runtime.get_memory_used() is not None

    def used(self):
        """Bytes currently allocated, including the base runtime — the same
        quantity the device's memUsed_ counter reports."""
        if not self.enabled:
            return 0
        return self.rt.get_memory_used(total=True) or 0

    def sample(self):
        """Fold current usage into the high-water mark.

        Every read of `peak` must be preceded by one of these. The device gets
        the invariant "peak >= used" for free — alloc() updates the mark on
        every single allocation — but here the sampler is a count hook, so a
        script that finishes between two firings, or one whose allocation
        happens entirely during compilation, would otherwise report a peak
        below its own live set: not a conservative error, an impossible one."""
        n = self.used()
        if n > self.peak:
            self.peak = n
        return n

    def install_hook(self, sethook):
        """Arm the count hook. `sethook` must be Lua's own debug.sethook,
        captured before the sandbox nils `debug` — and the sampler has to be a
        Lua closure, because sethook rejects a bare Python callable."""
        if not self.enabled or sethook is None:
            return
        self.rt.globals()["__mem_sample"] = self.sample
        # Wrap in Lua so sethook gets a Lua function, then drop the global so
        # scripts cannot see it.
        hook = self.rt.eval("function() __mem_sample() end")
        sethook(hook, "", self.HOOK_EVERY)

    # -- per-module accounting, driven by require() --------------------------

    def enter_module(self, name):
        self._stack.append((name, self.used()))

    def exit_module(self):
        name, before = self._stack.pop()
        self.modules.append((len(self._stack), name, self.used() - before))

    def device_estimate(self, host_bytes):
        return int(host_bytes * DEVICE_RATIO)

    def report(self, stream, budget=DEVICE_CAP, ceiling=0):
        """Print the run's memory profile.

        Note what `peak` is and is not. It is the high-water mark of *allocated*
        bytes, which includes garbage Lua had not collected yet — so it runs
        well above what the script actually needs (~1.7x for adventure.lua).
        It is NOT the failure threshold: when the allocator refuses, Lua runs an
        emergency full GC and retries (lib/Lua/lmem.c), so a script survives a
        cap far below its free-running peak. To ask "does this fit?", impose the
        ceiling and see if it survives (--fail-over-budget), or measure the
        headroom exactly with --min-heap.
        """
        if not self.enabled:
            print("memory: not measured (counting allocator disabled)", file=stream)
            return
        # Before the collect below, not after: peak must include whatever is
        # allocated right now, and gccollect() would have thrown it away.
        self.sample()
        final = self.used()
        # One full collect after the run costs nothing and perturbs nothing, and
        # turns "allocated" into the live set that actually has to fit.
        self.rt.gccollect()
        live = self.used()
        print("", file=stream)
        print("---- Lua memory ----------------------------------------", file=stream)
        if self.modules:
            for depth, name, cost in self.modules:
                pad = "  " * depth
                print(f"  require {pad}{name:<26} {fmt_kb(cost):>10}", file=stream)
        print(f"  peak allocated (host)  {fmt_kb(self.peak):>10}"
              f"   incl. uncollected garbage", file=stream)
        print(f"  final allocated (host) {fmt_kb(final):>10}", file=stream)
        print(f"  final live (host)      {fmt_kb(live):>10}"
              f"   after a full collect", file=stream)
        print(f"  device estimate        {fmt_kb(self.device_estimate(live)):>10}"
              f"   (live x{DEVICE_RATIO}) vs {fmt_kb(budget)} cap", file=stream)
        if ceiling:
            print(f"  ran under a {fmt_kb(ceiling)} host ceiling"
                  f" = {fmt_kb(int(ceiling * DEVICE_RATIO))} device", file=stream)
        else:
            print(f"  for a pass/fail answer: --fail-over-budget, or --min-heap",
                  file=stream)
        # The SD read cap is the other ceiling a growing game walks into, and it
        # is a per-file one the Lua totals above cannot show. Name the worst
        # offender so it is visible long before it becomes a hard failure.
        if SOURCE_READS:
            path, size = max(SOURCE_READS, key=lambda r: r[1])
            print(f"  largest single file    {fmt_kb(size):>10}"
                  f"   {100.0 * size / MAX_READ_BYTES:.0f}% of the {fmt_kb(MAX_READ_BYTES)}"
                  f" SD read cap ({path})", file=stream)
        print("--------------------------------------------------------", file=stream)


def load_font(px):
    for name in ("segoeui.ttf", "arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, px)
        except OSError:
            continue
    return ImageFont.load_default()


class ScriptAborted(Exception):
    """Long-hold Back / device.exit / key queue exhausted — device abort."""


class Sim:
    def __init__(self, args):
        dw, dh = parse_device(args.device)
        self.width = args.width or dw
        self.height = args.height or dh
        self.device = args.device
        self.img = Image.new("L", (self.width, self.height), WHITE)
        self.draw = ImageDraw.Draw(self.img)
        self._patterns = {}  # ink level → screen-sized dither field, built on demand
        self.quantize = not getattr(args, "no_quantize", False)
        self.fonts = {k: load_font(v) for k, v in FONT_SIZES.items()}
        self.frames_dir = args.frames
        self.frame_no = 0
        self.headless = args.keys is not None
        self.keys = [k.strip() for k in args.keys.split(",")] if args.keys else []
        # Default SD layout: the script's own directory *is* /scripts, so relative
        # assets (e.g. adventure/player.bmp) just work. --sd overrides with a full
        # SD-card-root directory.
        self.sd_root = os.path.abspath(args.sd) if args.sd else None
        self.scripts_dir = os.path.dirname(os.path.abspath(args.script))
        self.start = time.monotonic()
        self.key_queue = queue.Queue()
        self.held = set()
        self._back_down_at = None  # Escape press time for soft vs long-hold
        self.tk_root = None
        self.tk_label = None
        if self.frames_dir:
            os.makedirs(self.frames_dir, exist_ok=True)

    # ---- screen -----------------------------------------------------------

    def _font(self, opts):
        size = (opts or {}).get("size") if opts else None
        return self.fonts.get(size or "medium", self.fonts["medium"])

    def _ink(self, v, dflt="black"):
        """Resolve a Lua ink argument the way ScriptBindings' inkArg() does:
        nil/absent → dflt, boolean → black/white, palette name → that level.
        lupa hands Lua nil to Python as None, which is why an explicit None must
        mean the default and not `false`."""
        if v is None:
            return dflt
        if isinstance(v, bool):
            return "black" if v else "white"
        return INK_LEVELS.get(str(v), dflt)

    def _pattern(self, level):
        """A screen-sized dither field, built once per level. Fills crop this at
        their destination coordinates, so the pattern keeps the same phase
        wherever it lands — matching the firmware, whose dither is keyed on
        absolute screen position rather than on the shape being drawn."""
        p = self._patterns.get(level)
        if p is None:
            # Both grays have period 2 in x and y, so two rows describe the field.
            rows = [bytes(BLACK if ink_inks(level, x, y) else WHITE
                          for x in range(self.width)) for y in (0, 1)]
            p = Image.frombytes("L", (self.width, self.height),
                                b"".join(rows[y % 2] for y in range(self.height)))
            self._patterns[level] = p
        return p

    def _fill(self, x0, y0, x1, y1, level):
        """Fill the inclusive box [x0,y0]..[x1,y1] at an ink level."""
        x0, y0 = max(0, int(x0)), max(0, int(y0))
        x1, y1 = min(self.width - 1, int(x1)), min(self.height - 1, int(y1))
        if x0 > x1 or y0 > y1:
            return
        if level in ("black", "white"):
            self.draw.rectangle((x0, y0, x1, y1), fill=BLACK if level == "black" else WHITE)
        else:
            self.img.paste(self._pattern(level).crop((x0, y0, x1 + 1, y1 + 1)), (x0, y0))

    def screen_clear(self, color=0xFF):
        # A palette name fills at that level; a number is a raw framebuffer byte.
        if isinstance(color, str):
            self._fill(0, 0, self.width - 1, self.height - 1, self._ink(color, "white"))
            return
        color = int(color) & 0xFF
        if color in (0x00, 0xFF):
            self.img.paste(BLACK if color == 0 else WHITE, (0, 0, self.width, self.height))
            return
        # The firmware memsets this byte across the 1bpp buffer, so it is a
        # repeating 8-pixel pattern, not a flag. Bit set = white, MSB first
        # (GfxRenderer.cpp drawPixel: bitPosition = 7 - phyX % 8, and `state`
        # CLEARS the bit). In Portrait the panel is rotated 90° so physical x is
        # logical y (rotateCoordinates: phyX = y) — hence horizontal stripes.
        row = bytes(WHITE if (color >> (7 - (y % 8))) & 1 else BLACK for y in range(self.height))
        self.img = Image.frombytes("L", (self.width, self.height),
                                   b"".join(bytes([v]) * self.width for v in row))
        self.draw = ImageDraw.Draw(self.img)

    def screen_text(self, x, y, text, opts=None):
        opts = dict(opts.items()) if opts is not None and not isinstance(opts, dict) else (opts or {})
        font = self._font(opts)
        text = str(text)
        stroke = 1 if opts.get("bold") else 0
        align = opts.get("align", "left")
        if align == "center":  # firmware centers on full screen width, ignoring x
            w = self.draw.textlength(text, font=font)
            x = (self.width - w) // 2
        elif align == "right":
            x -= self.draw.textlength(text, font=font)
        self.draw.text((x, y), text, font=font, fill=BLACK, stroke_width=stroke)

    def screen_line(self, x1, y1, x2, y2, width=1):
        self.draw.line((x1, y1, x2, y2), fill=BLACK, width=int(width))

    def screen_rect(self, x, y, w, h, fill=False, ink=None):
        if w <= 0 or h <= 0:
            return
        if fill:
            self._fill(x, y, x + w - 1, y + h - 1, self._ink(ink, "black"))
        else:
            self.draw.rectangle((x, y, x + w - 1, y + h - 1), outline=BLACK)

    def screen_pixel(self, x, y, on=None):
        x, y = int(x), int(y)
        if 0 <= x < self.width and 0 <= y < self.height:
            # A single pixel at a gray level only inks where that level's
            # pattern falls, so it may draw nothing at all — as on device.
            self.img.putpixel((x, y), BLACK if ink_inks(self._ink(on, "black"), x, y) else WHITE)

    def screen_image(self, path, x, y, w=None, h=None):
        try:
            bmp = Image.open(self._host_path(path)).convert("L")
        except OSError:
            print(f"[sim] screen.image: cannot open {path} (looked in {self._host_path(path)})",
                  file=sys.stderr)
            return False
        bmp = bmp.resize((int(w or self.width), int(h or self.height)))
        self.img.paste(bmp, (int(x), int(y)))
        return True

    def screen_save(self, path, x=0, y=0, w=None, h=None):
        """screen.save mirror: write a region of the framebuffer as a 1-bit BMP,
        clipped to the screen, write confined to /scripts like the firmware.
        Saves the *presented* (quantized) image — the device framebuffer is
        already 1bpp, so this is what its save sees too."""
        p = self._writable(path)
        x, y = int(x), int(y)
        w = int(w if w is not None else self.width)
        h = int(h if h is not None else self.height)
        if x < 0:
            w += x
            x = 0
        if y < 0:
            h += y
            y = 0
        w = min(w, self.width - x)
        h = min(h, self.height - y)
        if w <= 0 or h <= 0:
            raise RuntimeError("screen.save: empty region")
        os.makedirs(os.path.dirname(p), exist_ok=True)
        self._presented().crop((x, y, x + w, y + h)).convert("1").save(p, "BMP")
        return True

    def screen_invert(self):
        self.img = Image.eval(self.img, lambda p: 255 - p)
        self.draw = ImageDraw.Draw(self.img)

    def _presented(self):
        """The framebuffer as the panel would actually show it. PIL renders text
        antialiased, but the device's glyphs are 1bpp — so without this pass a
        dumped frame mixes real dither with font gray and neither can be checked.
        Applied only on the way out; drawing itself stays in "L"."""
        if not self.quantize:
            return self.img
        return self.img.point(lambda p: WHITE if p >= 128 else BLACK)

    def screen_update(self, mode=None):
        if self.frames_dir:
            self.frame_no += 1
            self._presented().save(os.path.join(self.frames_dir, f"frame_{self.frame_no:04d}.png"))
        if self.tk_root is not None:
            self._tk_show()

    # ---- input --------------------------------------------------------------

    def _next_scripted(self, blocking):
        while self.keys:
            tok = self.keys[0]
            if tok.startswith("nil:"):
                n = int(tok.split(":")[1])
                if not blocking and n > 0:  # consume one nil poll
                    self.keys[0] = f"nil:{n - 1}" if n > 1 else None
                    if self.keys[0] is None:
                        self.keys.pop(0)
                    return None
                self.keys.pop(0)  # wait() skips nil runs
                continue
            self.keys.pop(0)
            if tok == "backhold":
                raise ScriptAborted()  # long-hold Back
            if tok in BUTTONS:
                return tok
            raise ScriptAborted()  # unknown token aborts
        raise ScriptAborted()  # queue exhausted = end of headless run

    def _check_back_hold(self):
        if self._back_down_at is not None and (time.monotonic() - self._back_down_at) >= BACK_HOLD_EXIT_S:
            raise ScriptAborted()

    def input_wait(self):
        if self.headless:
            return self._next_scripted(blocking=True)
        while True:
            self._check_back_hold()
            try:
                key = self.key_queue.get(timeout=0.05)
            except queue.Empty:
                self._check_alive()
                continue
            return key

    def input_poll(self):
        if self.headless:
            try:
                return self._next_scripted(blocking=False)
            except ScriptAborted:
                raise
        self._check_alive()
        self._check_back_hold()
        try:
            return self.key_queue.get_nowait()
        except queue.Empty:
            return None

    def device_exit(self):
        raise ScriptAborted()

    def input_down(self, name):
        return name in self.held

    # ---- fs (writes confined to /scripts, like the device) -----------------

    def _host_path(self, path):
        p = path if path.startswith("/") else "/scripts/" + path
        if self.sd_root:
            return os.path.join(self.sd_root, p.lstrip("/"))
        if p == "/scripts" or p.startswith("/scripts/"):
            return os.path.join(self.scripts_dir, p[len("/scripts/"):])
        return os.path.join(self.scripts_dir, p.lstrip("/"))  # best effort outside /scripts

    def _writable(self, path):
        p = path if path.startswith("/") else "/scripts/" + path
        if ".." in p or not (p == "/scripts" or p.startswith("/scripts/")):
            raise RuntimeError("writes are limited to /scripts")
        return self._host_path(p)

    def fs_read(self, path):
        try:
            with open(self._host_path(path), "rb") as f:
                raw = f.read()
        except OSError:
            return None
        # Raise rather than hand back a short string, as the firmware does:
        # `nil` already means "missing" here, and `fs.read(p) or default` is a
        # common idiom, so either would swallow a half file silently.
        check_read_size(path, len(raw), "fs.read")
        return raw.decode("utf-8", "replace")

    # fs.readRange cap, mirroring the firmware constant (ScriptBindings.cpp
    # kMaxRangeRead). A range read exists to keep large buffers out of the
    # heap, so a large range through it is a caller bug on both targets.
    MAX_RANGE_READ = 4096

    def fs_readrange(self, path, offset, length):
        offset = int(offset)
        length = int(length)
        if offset < 0:
            raise RuntimeError(f"fs.readRange: negative offset ({offset})")
        if length < 0 or length > self.MAX_RANGE_READ:
            raise RuntimeError(f"fs.readRange: len {length} out of range (0..{self.MAX_RANGE_READ})")
        try:
            with open(self._host_path(path), "rb") as f:
                f.seek(offset)
                raw = f.read(length)
        except OSError:
            return None
        # Short read = wrong offsets. Raise like the firmware: nil stays
        # reserved for "no such file", and half a record must not look whole.
        if len(raw) != length:
            raise RuntimeError(f"fs.readRange: {path}: wanted {length} bytes at {offset}, got {len(raw)}")
        return raw.decode("utf-8", "replace")

    def fs_write(self, path, data):
        p = self._writable(path)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", encoding="utf-8") as f:
            f.write(str(data))
        return True

    def fs_append(self, path, data):
        p = self._writable(path)
        # The device appends by reading the file whole, concatenating and
        # writing it back, so past the cap it does not just lose the tail — it
        # writes the truncation back. The firmware refuses; refuse here too,
        # even though this implementation could append in place.
        if os.path.exists(p):
            check_read_size(path, os.path.getsize(p), "fs.append refuses to rewrite the file truncated")
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "a", encoding="utf-8") as f:
            f.write(str(data))
        return True

    def fs_remove(self, path):
        try:
            os.remove(self._writable(path))
            return True
        except OSError:
            return False

    def fs_mkdir(self, path):
        os.makedirs(self._writable(path), exist_ok=True)
        return True

    def fs_exists(self, path):
        return os.path.exists(self._host_path(path))

    def fs_list(self, dir=None):
        d = self._host_path(dir or "/scripts")
        try:
            return [n + "/" if os.path.isdir(os.path.join(d, n)) else n for n in sorted(os.listdir(d))]
        except OSError:
            return []

    # ---- http / device ------------------------------------------------------

    def http_get(self, url):
        try:
            with urllib.request.urlopen(url, timeout=5) as r:
                return r.read().decode("utf-8", "replace"), None
        except Exception as e:  # noqa: BLE001 — mirror device's (nil, err) contract
            return None, str(e)

    def device_sleep(self, ms):
        if self.headless:
            return
        end = time.monotonic() + ms / 1000
        while time.monotonic() < end:
            self._check_alive()
            self._check_back_hold()
            time.sleep(0.01)

    def device_millis(self):
        return int((time.monotonic() - self.start) * 1000)

    # ---- window --------------------------------------------------------------

    def _check_alive(self):
        if self.tk_root is None and not self.headless:
            raise ScriptAborted()

    def _tk_show(self):
        self._pending = self._presented().copy()

    def run_interactive(self, script_path, lua_thread_fn, meter=None):
        import tkinter as tk
        from PIL import ImageTk

        self.tk_root = tk.Tk()
        self.tk_root.title(f"cross-trmnl sim — {os.path.basename(script_path)}")

        # Device picker: restarts the sim at the chosen panel size so you can
        # see how the script looks on each target device.
        bar = tk.Frame(self.tk_root)
        bar.pack(fill="x")
        tk.Label(bar, text="Device:").pack(side="left", padx=(4, 0))
        choices = list(DEVICES)
        if self.device not in choices:
            choices.append(self.device)
        dev_var = tk.StringVar(value=self.device)

        def switch_device(choice):
            if choice == self.device:
                return
            import subprocess
            argv, skip = [], False
            for a in sys.argv[1:]:
                if skip:
                    skip = False
                    continue
                if a in ("--device", "--width", "--height"):
                    skip = True
                    continue
                if a.startswith(("--device=", "--width=", "--height=")):
                    continue
                argv.append(a)
            subprocess.Popen([sys.executable, os.path.abspath(__file__)] + argv + ["--device", choice])
            self.tk_root.destroy()

        picker = tk.OptionMenu(bar, dev_var, *choices, command=switch_device)
        picker.configure(takefocus=0)  # keep keyboard focus on the framebuffer keys
        picker.pack(side="left", padx=2)
        tk.Label(bar, text=f"{self.width}x{self.height}").pack(side="left", padx=4)

        mem_label = None
        if meter is not None and meter.enabled:
            mem_label = tk.Label(bar, text="")
            mem_label.pack(side="right", padx=6)

        canvas = tk.Label(self.tk_root, bd=0)
        canvas.pack()
        self._pending = self._presented().copy()

        keymap = {"Up": "up", "Down": "down", "Left": "left", "Right": "right",
                  "Return": "confirm", "space": "confirm", "p": "power", "Escape": "back"}

        def on_key(e):
            k = keymap.get(e.keysym)
            if not k:
                return
            if k == "back":
                # Defer until release (soft) or hold threshold (force-quit).
                if self._back_down_at is None:
                    self._back_down_at = time.monotonic()
                self.held.add("back")
                return
            self.held.add(k)
            self.key_queue.put(k)

        def on_release(e):
            k = keymap.get(e.keysym)
            if k == "back" and self._back_down_at is not None:
                held = time.monotonic() - self._back_down_at
                self._back_down_at = None
                self.held.discard("back")
                if held < BACK_HOLD_EXIT_S:
                    self.key_queue.put("back")
                # else: long-hold already aborted via _check_back_hold, or will
                return
            self.held.discard(k)

        self.tk_root.bind("<KeyPress>", on_key)
        self.tk_root.bind("<KeyRelease>", on_release)
        self.tk_root.lift()
        self.tk_root.attributes("-topmost", True)
        self.tk_root.after(200, lambda: self.tk_root.attributes("-topmost", False))
        self.tk_root.focus_force()  # window must have keyboard focus for the keys to work

        worker = threading.Thread(target=lua_thread_fn, daemon=True)
        worker.start()

        def refresh():
            photo = ImageTk.PhotoImage(self._pending)
            canvas.configure(image=photo)
            canvas.image = photo
            if mem_label is not None:
                # Read the plain int only — never call into the runtime here.
                # The Lua worker thread is concurrently inside lua.execute.
                peak = meter.peak
                mem_label.configure(
                    text=f"peak {fmt_kb(peak)} host / "
                         f"~{fmt_kb(int(peak * DEVICE_RATIO))} device")
            if not worker.is_alive():
                self.tk_root.after(400, self.tk_root.destroy)
                return
            self.tk_root.after(50, refresh)

        refresh()
        try:
            self.tk_root.mainloop()
        finally:
            self.tk_root = None  # signals the Lua thread to abort


def make_runtime(sim, max_mem, budget=DEVICE_CAP):
    # max_memory=0 means "counted, no ceiling". Passing None instead would make
    # lupa install Lua's *default* allocator, and then get_memory_used() returns
    # None forever — which is why this simulator never reported memory before.
    lua = lua54.LuaRuntime(max_memory=max_mem, unpack_returned_tuples=True)
    g = lua.globals()
    # Grab debug.sethook for the memory meter before the sandbox removes debug.
    sethook = g.debug.sethook if g.debug is not None else None
    # match the device sandbox: no io/os/require/debug/load. collectgarbage goes
    # too — the firmware's lockdownGlobals removes it, so a script that calls it
    # must fail here as well rather than passing in the sim and erroring on
    # hardware.
    for name in ("io", "os", "require", "dofile", "loadfile", "load", "debug",
                 "package", "collectgarbage"):
        g[name] = None
    meter = MemoryMeter(lua)
    t = lua.table

    # Sample on every frame as well as from the count hook: a frame boundary is
    # where a script's live set is at its most interesting, and short scripts
    # may never execute enough VM instructions for the hook to fire at all.
    def metered_update(mode=None):
        meter.sample()
        return sim.screen_update(mode)

    g.screen = t(clear=sim.screen_clear, text=sim.screen_text, line=sim.screen_line,
                 rect=sim.screen_rect, pixel=sim.screen_pixel, image=sim.screen_image,
                 save=sim.screen_save, width=lambda: sim.width, height=lambda: sim.height,
                 invert=sim.screen_invert, update=metered_update)
    g.input = t(wait=sim.input_wait, poll=sim.input_poll, down=sim.input_down)
    g.fs = t(read=sim.fs_read, readRange=sim.fs_readrange, write=sim.fs_write,
             append=sim.fs_append, remove=sim.fs_remove, mkdir=sim.fs_mkdir,
             exists=sim.fs_exists, list=lambda d=None: lua.table(*sim.fs_list(d)))

    def http_get(url):
        body, err = sim.http_get(url)
        return (body,) if body is not None else (None, err)

    g.http = t(get=http_get)

    # sandboxed require, mirroring the firmware: /scripts/engine/<name>.lua
    # (the shared game engine), then /scripts/<name>.lua (game data modules)
    loaded = {}

    def lua_require(name):
        name = str(name)
        if not all(c.isalnum() or c in "._" for c in name):
            raise RuntimeError(f"invalid module name '{name}'")
        if name in loaded:
            return loaded[name]          # cached: costs nothing, so not metered
        rel = name.replace(".", "/")
        # Same order as the firmware: engine root before game root, and .luac
        # before .lua within each. A compiled module is found here rather than
        # skipped, so a mixed tree reports what it actually is instead of
        # "module not found" for a file sitting right there.
        found = path = None
        for root in ("/scripts/engine/", "/scripts/"):
            for ext in (".luac", ".lua"):
                candidate = root + rel + ext
                host = sim._host_path(candidate)
                if os.path.exists(host):
                    found, path = candidate, host
                    break
            if found:
                break
        if not found:
            raise RuntimeError(f"module '{name}' not found — is /scripts/engine on the card? "
                               f"(looked for {rel}.luac and {rel}.lua in /scripts/engine/ and /scripts/)")
        check_read_size(found, os.path.getsize(path), f"module '{name}' too large")
        # Nested requires nest here too, so the depth stack attributes each
        # module's own cost as well as the cost it pulls in.
        meter.enter_module(name)
        try:
            result = lua.execute(read_source(path, f"module '{name}'"))
        finally:
            meter.exit_module()
        meter.sample()
        loaded[name] = True if result is None else result
        return loaded[name]

    g.require = lua_require
    # device.mem() mirrors the firmware binding: used, peak, limit in bytes.
    # With no ceiling imposed there is nothing to enforce, but the device always
    # has a real limit — so report the host equivalent of the budget being
    # measured against, and `used / limit` means the same thing in both places.
    mem_limit = max_mem or int(budget / DEVICE_RATIO)

    def device_mem():
        # Sample first: the device's peak is exact by construction, so a script
        # asking here must not be told a stale one.
        meter.sample()
        # Fourth value is the device's free heap at launch, which the firmware
        # derives its limit from. The host has no such number, and synthesising
        # one (limit + reserve) would put an invented figure in the exact slot a
        # measurement belongs — arithmetic that agrees with itself and with
        # nothing real. Report 0 and let scripts say "not measured here".
        return meter.used(), meter.peak, mem_limit, 0

    g.device = t(battery=lambda: 100, sleep=sim.device_sleep, millis=sim.device_millis,
                 mac=lambda: "AA:BB:CC:DD:EE:FF", version=lambda: "sim",
                 log=lambda s: print(f"[device.log] {s}"), exit=sim.device_exit,
                 mem=device_mem)
    meter.install_hook(sethook)
    return lua, meter


def run_min_heap(args):
    """Bisect --max-mem to find the smallest heap the script survives on.

    Each probe runs in a SUBPROCESS on purpose: a cap below the sandbox's own
    base cost trips Lua's unprotected panic, which aborts the process outright
    rather than raising something catchable.
    """
    import subprocess

    base = [a for a in sys.argv[1:] if a != "--min-heap"]
    base = [a for a in base if not a.startswith("--max-mem")]
    # Drop a "--max-mem N" pair if it was given separately.
    out = []
    skip = False
    for a in base:
        if skip:
            skip = False
            continue
        if a == "--max-mem":
            skip = True
            continue
        out.append(a)
    base = out
    if "--keys" not in " ".join(base):
        print("--min-heap needs --keys (it must run headless)", file=sys.stderr)
        return 1

    # A probe just above the true minimum spends its life in Lua's emergency-GC
    # retry loop: every allocation refuses, forces a full collect and retries.
    # That is a practical failure on a 160 MHz single core even though it would
    # eventually finish, so treat a probe that will not complete promptly as
    # "does not survive" rather than waiting for it.
    PROBE_TIMEOUT_S = 60

    def survives(cap):
        try:
            p = subprocess.run([sys.executable, os.path.abspath(__file__)] + base
                               + ["--max-mem", str(cap), "--no-mem"],
                               capture_output=True, text=True, timeout=PROBE_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            return False
        return p.returncode == 0

    lo, hi = 16 * 1024, 16 * 1024
    probes = 0
    while not survives(hi):                     # find any working ceiling
        lo, hi = hi, hi * 2
        probes += 1
        if hi > 64 * 1024 * 1024:
            print("script fails even at 64 MB — not a memory limit", file=sys.stderr)
            return 1
    while hi - lo > 512:                        # then narrow it
        mid = (lo + hi) // 2
        probes += 1
        if survives(mid):
            hi = mid
        else:
            lo = mid
    device = int(hi * DEVICE_RATIO)
    pct = 100.0 * device / args.device_budget
    print(f"minimum viable heap  host {fmt_kb(hi)}  ->  device ~{fmt_kb(device)}"
          f"   ({pct:.0f}% of the {fmt_kb(args.device_budget)} cap)   [{probes} probes]")
    if pct > 100:
        print("OVER BUDGET — this script will not run on the device", file=sys.stderr)
        return 2 if args.fail_over_budget else 0
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("script", help="path to a .lua script")
    ap.add_argument("--keys", help="comma-separated key sequence; enables headless mode")
    ap.add_argument("--frames", help="directory to dump a PNG per screen.update()")
    ap.add_argument("--sd", default=None,
                    help="directory acting as the SD card root; default maps /scripts "
                         "to the script's own directory so relative assets just work")
    ap.add_argument("--device", default="x4",
                    help="preview panel: " + ", ".join(f"{k} ({w}x{h})" for k, (w, h) in DEVICES.items())
                         + ", or an explicit WxH (default x4). The window also has a "
                         "Device dropdown that restarts the script at the picked size.")
    ap.add_argument("--width", type=int, help="override the panel width")
    ap.add_argument("--height", type=int, help="override the panel height")
    ap.add_argument("--max-mem", type=int, default=0,
                    help=f"Lua memory ceiling in bytes (0 = measured but unlimited). This "
                         f"host runs 64-bit Lua, where the same script costs more than on "
                         f"the device's 32-bit build — the measured ratio is "
                         f"{DEVICE_RATIO}, so the device's {fmt_kb(DEVICE_CAP)} cap is "
                         f"about {HOST_BUDGET} here. Use --device-budget to say it in "
                         f"device bytes instead.")
    ap.add_argument("--device-budget", type=int, default=DEVICE_CAP, metavar="BYTES",
                    help=f"device-side memory budget the report is measured against "
                         f"(default {DEVICE_CAP}, the firmware's cap).")
    ap.add_argument("--no-mem", action="store_true",
                    help="suppress the memory report.")
    ap.add_argument("--fail-over-budget", action="store_true",
                    help="exit 2 if the estimated device peak exceeds --device-budget. "
                         "Exit codes 0 (ok) and 1 (script error) are unchanged, so "
                         "existing runners are unaffected.")
    ap.add_argument("--min-heap", action="store_true",
                    help="find the smallest heap the script survives on, by bisecting "
                         "--max-mem, and report it in device bytes. This is the truest "
                         "gate: Lua runs an emergency full GC and retries whenever the "
                         "allocator refuses (lib/Lua/lmem.c), so a script survives a cap "
                         "well below its free-running peak.")
    ap.add_argument("--no-quantize", action="store_true",
                    help="keep PIL's antialiased text instead of thresholding the frame "
                         "to 1bpp. The panel is 1 bit per pixel, so quantizing is the "
                         "faithful default; turn it off only for a smoother preview, and "
                         "never when checking dithered output, since font gray is then "
                         "indistinguishable from a real gray level.")
    args = ap.parse_args()

    if args.min_heap:
        sys.exit(run_min_heap(args))

    # --fail-over-budget answers "does this fit?" the way the device does:
    # impose the equivalent ceiling and see whether the script survives it.
    # No estimation of a peak is involved — the allocator enforces it, and Lua's
    # emergency GC gets the same chance to recover that it gets on hardware.
    ceiling = args.max_mem
    if args.fail_over_budget and not ceiling:
        ceiling = int(args.device_budget / DEVICE_RATIO)

    sim = Sim(args)
    lua, meter = make_runtime(sim, ceiling, args.device_budget)
    try:
        check_read_size(args.script, os.path.getsize(args.script), "script too large")
        source = read_source(args.script, "script")
    except RuntimeError as e:
        sys.exit(f"script error: {e}")

    result = {"error": None}

    def run():
        try:
            lua.execute(source)
        except ScriptAborted:
            print("script aborted (Back)")
        except MemoryError:
            result["error"] = "not enough memory (host cap; see --max-mem)"
            print(f"script error: {result['error']}", file=sys.stderr)
        except Exception as e:  # noqa: BLE001 — report Lua errors like the device does
            msg = str(e) or type(e).__name__
            if "ScriptAborted" in msg:
                print("script aborted (Back)")
            else:
                result["error"] = msg
                print(f"script error: {msg}", file=sys.stderr)

    if sim.headless:
        run()
    else:
        sim.run_interactive(args.script, run, meter)

    if not args.no_mem:
        meter.report(sys.stderr, args.device_budget, ceiling)
    if result["error"]:
        # An out-of-memory failure under an imposed budget is a budget failure
        # (exit 2), not a broken script (exit 1) — that distinction is what lets
        # a route runner tell "too big" from "crashed".
        if args.fail_over_budget and "not enough memory" in result["error"]:
            print(f"FAIL: does not fit {fmt_kb(args.device_budget)} on the device "
                  f"({fmt_kb(ceiling)} host ceiling)", file=sys.stderr)
            sys.exit(2)
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
