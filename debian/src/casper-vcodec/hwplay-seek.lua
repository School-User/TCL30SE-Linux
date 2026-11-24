-- hwplay seeking: the video arrives through a pipe, so mpv cannot seek itself.
-- A seek writes the target time (seconds, absolute) to hwplay's seek file and stops playback; hwplay feeds the new position into this same window.
-- Keys: LEFT/RIGHT -/+10 s, DOWN/UP -/+60 s. Touch: double-tap left/right third -/+10 s, middle = pause.
-- hwplay-offset changes on every seek (hwplay updates script-opts), so read it at use time.
local function offset() return tonumber(mp.get_opt("hwplay-offset") or "0") or 0 end
local function seekfile() return mp.get_opt("hwplay-file") or "" end

local function jump(delta)
	local t = math.max(0, offset() + (mp.get_property_number("playback-time") or 0) + delta)
	local f = io.open(seekfile(), "w")
	if not f then return end
	f:write(string.format("%.3f\n", t))
	f:close()
	mp.osd_message(string.format("seeking to %d:%02d", math.floor(t / 60), math.floor(t % 60)), 3)
	mp.commandv("stop")	-- hwplay keeps this window and loads the new position into it
end

for key, d in pairs({ RIGHT = 10, LEFT = -10, UP = 60, DOWN = -60 }) do
	mp.add_forced_key_binding(key, "hwplay-" .. key, function() jump(d) end)
end

-- fill (cropped) <-> fit (whole picture with bars); key z, or double-tap the top-right corner
local function zoom()
	local p = mp.get_property_number("panscan") or 0
	mp.set_property_number("panscan", p > 0 and 0 or 1)
	mp.osd_message(p > 0 and "fit" or "fill", 1)
end
mp.add_forced_key_binding("z", "hwplay-zoom", zoom)

mp.add_forced_key_binding("MBTN_LEFT_DBL", "hwplay-tap", function()
	local m = mp.get_property_native("mouse-pos") or {}
	local x, y = m.x or 0, m.y or 0
	local w, h = mp.get_property_number("osd-width") or 1, mp.get_property_number("osd-height") or 1
	if x > w * 2 / 3 and y < h / 4 then zoom()
	elseif x < w / 3 then jump(-10) elseif x > w * 2 / 3 then jump(10) else mp.commandv("cycle", "pause") end
end)

-- show the real position (playback-time restarts at 0 after every seek)
mp.add_forced_key_binding("o", "hwplay-pos", function()
	local t = offset() + (mp.get_property_number("playback-time") or 0)
	mp.osd_message(string.format("%d:%02d", math.floor(t / 60), math.floor(t % 60)), 2)
end)

