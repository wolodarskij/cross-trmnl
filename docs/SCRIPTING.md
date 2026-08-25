# Script Execution (Lua)

cross-trmnl can run **Lua 5.4** scripts from the SD card. Put `.lua` files in a
`/scripts` folder on the card, then open **Scripts** from the home menu, pick
one, and press Confirm to run it. **Back always stops a running script** and
returns to the list — this is the universal "quit" button, so scripts use the
other buttons for interaction.

Copy the samples in [`examples/scripts/`](../examples/scripts) onto the card to
get started.

## Safety

Scripts are sandboxed and cannot brick the device:

- **Back aborts** any script, even one stuck in an infinite loop (a VM hook
  polls the button between instructions).
- **Memory is capped** (~96 KB); a script that allocates too much fails
  cleanly with "not enough memory" instead of crashing the firmware.
- **Errors are caught** and shown on screen with a traceback.
- **File writes are confined to `/scripts`** (reads can be anywhere on the SD
  card), so a script can't overwrite system files.
- No `os.execute`, `io`, `require`/`loadlib`, or `debug` — only the safe
  standard libraries (`string`, `table`, `math`, `utf8`, `coroutine`) plus the
  device API below.

## Language

Standard Lua 5.4 with 32-bit integers and 32-bit floats (the ESP32-C3 has no
FPU). `string`, `table`, `math`, `utf8`, and `coroutine` are available.
`print(...)` and `device.log(...)` write to the serial monitor.

## Device API

### `screen` — draw on the e-ink display
Drawing does not appear until you call `screen.update()`.

| Function | Description |
|---|---|
| `screen.clear([color])` | Fill the screen. `color` omitted = white; pass `0x00` for black. |
| `screen.text(x, y, str [, opts])` | Draw text. `opts` = `{size=, bold=, align=}`. `size` is `"small"`/`"medium"`/`"large"`. `align` is `"left"` (default), `"center"`, or `"right"`. |
| `screen.line(x1, y1, x2, y2 [, width])` | Draw a line. |
| `screen.rect(x, y, w, h [, fill])` | Rectangle; `fill=true` for a solid block. |
| `screen.pixel(x, y [, on])` | Set a pixel (`on=false` clears it). |
| `screen.image(path, x, y [, w, h])` | Draw a BMP file, scaled to fit `w`×`h` (default: full screen). Returns `true` on success. |
| `screen.width()` / `screen.height()` | Screen size in pixels. |
| `screen.invert()` | Invert the whole framebuffer. |
| `screen.update([mode])` | Push the framebuffer to the panel. `mode` = `"fast"` (default), `"full"` (cleanest, slower), or `"half"`. |

### `input` — buttons
Button names: `"confirm"`, `"left"`, `"right"`, `"up"`, `"down"`, `"power"`.
(`"back"` is reserved — pressing Back stops the script.)

| Function | Description |
|---|---|
| `input.wait()` | Block until a button is pressed; returns its name. |
| `input.poll()` | Non-blocking; returns a button name or `nil`. |
| `input.down(name)` | `true` while that button is held. |

### `fs` — SD card files
Writes/removes are limited to `/scripts`; relative paths resolve there.

| Function | Description |
|---|---|
| `fs.read(path)` | Return file contents as a string, or `nil` if missing. |
| `fs.write(path, str)` | Overwrite a file. Returns `true` on success. |
| `fs.append(path, str)` | Append to a file. |
| `fs.remove(path)` | Delete a file. |
| `fs.mkdir(path)` | Create a directory. |
| `fs.exists(path)` | `true` if the path exists. |
| `fs.list([dir])` | Table of entry names (directories end with `/`). Default `dir` is `/scripts`. |

### `http` — network
| Function | Description |
|---|---|
| `http.get(url)` | GET a URL; returns the body as a string, or `nil, error`. WiFi is connected automatically from saved networks on first use, and torn down when the script exits. Use `http://` for LAN servers. |

### `device` — device info & timing
| Function | Description |
|---|---|
| `device.battery()` | Battery percentage (integer). |
| `device.sleep(ms)` | Sleep, still interruptible with Back. |
| `device.millis()` | Milliseconds since boot. |
| `device.mac()` | WiFi MAC address string. |
| `device.version()` | Firmware version string. |
| `device.log(str)` | Write a line to the serial monitor. |

## Example

```lua
-- clock-ish greeting with a battery bar
local pct = device.battery()
screen.clear()
screen.text(screen.width() // 2, 60, "Good day!", {size = "large", bold = true, align = "center"})
screen.text(20, 140, "Battery " .. pct .. "%", {size = "medium"})
screen.rect(20, 175, screen.width() - 40, 24)
screen.rect(20, 175, (screen.width() - 40) * pct // 100, 24, true)
screen.update("full")
input.wait()
```

See [`examples/scripts/`](../examples/scripts) for `hello.lua`, `battery.lua`,
`paint.lua`, and `http_dashboard.lua`.
