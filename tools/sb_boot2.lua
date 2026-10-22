-- sb_boot2.lua: the 68000s' accesses to the DSP control area (0x200000-0x2003ff)
-- from power-on, logged only when the value changes (polling loops collapse).
local screen = manager.machine.screens[":screen"]
local out = io.open("sbboot2.txt", "w")
local last = {}
local function log(kind, tag, offset, data)
    local key = kind .. tag .. offset
    if last[key] ~= data then
        last[key] = data
        out:write(string.format("%s %s %d %06x %04x\n", kind, tag, screen:frame_number(), offset, data))
        out:flush()
    end
end
local function taps(tag)
    local sp = manager.machine.devices[tag].spaces["program"]
    return {
        sp:install_read_tap(0x200000, 0x2003ff, "b2r" .. tag, function(o, d, m) log("R", tag, o, d) end),
        sp:install_write_tap(0x200000, 0x2003ff, "b2w" .. tag, function(o, d, m) log("W", tag, o, d) end),
    }
end
sbb2_a = taps(":maincpu")
sbb2_b = taps(":slave")
