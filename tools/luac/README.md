# luac — precompile scripts for the device

Compiles `/scripts` Lua to bytecode the firmware loads directly. Two wins, and
neither of them is the file size:

- **The parser never runs on the device.** Loading source peaks at roughly
  **1.28×** the memory the chunk finally occupies, and that transient peak is
  what kills a load that would otherwise have fitted.
- **`-s` drops the debug arrays** — line info, local variable records, upvalue
  names — which are about **17 KB** on `adventure.lua`, permanently.

## Use

```bash
python tools/luac/build.py         # build the host compiler (once)
python tools/luac/compile_tree.py  # lua-scripts-src -> lua-scripts-build
python tools/luac/selftest.py      # prove the output is device-loadable
```

Copy `lua-scripts-build/` onto the card. `require` prefers `.luac` over `.lua`
within the same directory, and the script browser lists both, so a card can
carry either or both — source for debugging, bytecode for running.

```bash
python tools/luac/compile_tree.py path/to/src -o path/to/out
python tools/luac/compile_tree.py --no-strip    # keep file:line in errors
```

The compiler itself:

```
luac [-s] -o <out.luac> <in.lua>   compile (-s strips debug info)
luac --check <file>...             verify each file loads as bytecode
luac --info                        print this build's numeric config
```

## Why it is built from lib/Lua, and cannot be a system luac

`lundump.c`'s `checkHeader` compares `sizeof(Instruction)`, `sizeof(lua_Integer)`
and `sizeof(lua_Number)` between the chunk and the loading VM. The firmware's
`lib/Lua/luaconf.h` hard-codes `LUA_32BITS 1`: 4-byte integers, `float`
numbers. A stock Lua 5.4 has 8-byte integers and `double`, so its output is
rejected on the device — and the device's output is rejected by it.

Building these exact sources gives the host tool the firmware's numeric config
*by construction*. There is no `-DLUA_32BITS` to pass, and therefore none to
forget. `luac --info` prints what it got:

```
Lua 5.4.7
  lua_Integer  4 bytes
  lua_Number   4 bytes  (float)
  LUA_32BITS   1
```

Pointer and `size_t` width are *not* part of the header check, which is why a
64-bit host is fine.

`selftest.py` demonstrates the rejection rather than asserting it, using the
64-bit Lua that `lupa` already ships — no second toolchain to build:

```
PASS  numeric config matches the device  — Lua 5.4.7
PASS  compile hello.lua
PASS  compiled output loads back
PASS  source is rejected as bytecode  — attempt to load a text chunk (mode is 'b')
PASS  stock 64-bit chunk is rejected  — Instruction=4 lua_Integer=8 lua_Number=8;
                                        bad binary format (lua_Integer size mismatch)
```

## The simulator runs source, not bytecode

`lupa` embeds that same stock 64-bit Lua, so it cannot load device chunks and
never will without shipping a second Lua inside a Python extension. That is not
a gap to close: `lua-scripts-src/` stays the single source of truth, the
simulator runs it, and the compiled tree is a deployment artifact verified with
`luac --check`. Pointing the simulator at a `.luac` says exactly that.

Because the source tree is the truth, `compile_tree.py` refuses to write into
it, or into any directory inside it.

## Requirements

A host C compiler. `build.py` looks for `cc`, `gcc` or `clang` on `PATH`, then
for MSVC through `vswhere` (the "Desktop development with C++" workload).
Nothing is downloaded. Output lands in `tools/luac/bin/`, which is gitignored —
it is built from `lib/Lua`, so it is never out of date with the firmware for
longer than one `build.py`.

## Limits that still apply

Bytecode is subject to the same **50,000-byte** whole-file read cap as source
(`SDCardManager::kMaxReadFileBytes`); `compile_tree.py` fails the build on any
output that exceeds it, with the same wording the firmware uses.

`require` accepting bytecode is a real widening of the sandbox: `lundump` does
not fully validate what it reads, so a corrupt `.luac` can misbehave where a
corrupt `.lua` would only be a syntax error. The card's contents are already as
trusted as the script itself — the main chunk has always loaded with mode
`"bt"` — but it is worth knowing.
