-- hello.lua — the simplest script: draw some text and wait.
-- Copy this file (and the others here) into /scripts on the SD card.

screen.clear()
screen.text(screen.width() // 2, 120, "Hello, X4!", {size = "large", bold = true, align = "center"})
screen.text(screen.width() // 2, 175, "cross-trmnl Lua scripting", {align = "center"})
screen.text(screen.width() // 2, screen.height() - 40, "Press Back to exit", {size = "small", align = "center"})
screen.update("full")

input.wait()  -- Back always exits a script
