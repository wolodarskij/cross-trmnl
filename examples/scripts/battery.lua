-- battery.lua — read device info and draw a battery gauge.

local pct = device.battery()

screen.clear()
screen.text(20, 40, "Battery", {size = "large", bold = true})
screen.text(20, 92, pct .. "%", {size = "large"})

local w = screen.width() - 40
screen.rect(20, 140, w, 34)                 -- outline
screen.rect(20, 140, w * pct // 100, 34, true)  -- filled portion

screen.text(20, 210, "Firmware: " .. device.version(), {size = "small"})
screen.text(20, 234, "MAC: " .. device.mac(), {size = "small"})
screen.text(20, screen.height() - 40, "Press Back to exit", {size = "small"})
screen.update("full")

input.wait()
