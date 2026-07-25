-- http_dashboard.lua — fetch text/JSON over WiFi and display it.
-- WiFi is brought up automatically (using saved networks) on the first
-- http.get; it is torn down when you leave the script.
--
-- Change URL to any endpoint on your network. A plain-text or JSON response
-- works best. Example points at a local x4-dashboard-server status check.

local URL = "http://192.168.178.20:8080/healthz"

screen.clear()
screen.text(20, 40, "Fetching...", {size = "large"})
screen.update("fast")

local body, err = http.get(URL)

screen.clear()
if not body then
  screen.text(20, 40, "Request failed", {size = "large", bold = true})
  screen.text(20, 90, tostring(err), {size = "small"})
else
  screen.text(20, 24, "Response from server:", {size = "large", bold = true})
  local y = 70
  for line in body:gmatch("[^\n]+") do
    screen.text(15, y, line, {size = "small"})
    y = y + 20
    if y > screen.height() - 50 then break end
  end
end

screen.text(20, screen.height() - 26, "Press Back to exit", {size = "small"})
screen.update("full")
input.wait()
