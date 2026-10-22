-- sb_reads.lua: what the 68000s read from the shared DSP RAM: address, value,
-- frame; boot (frames 0-600) and one stretch of play (3000-3002).
local screen = manager.machine.screens[":screen"]
local out = io.open("sbreads.txt", "w")
local function tap(tag)
    local sp = manager.machine.devices[tag].spaces["program"]
    return sp:install_read_tap(0x200000, 0x20ffff, "sbreads" .. tag, function(offset, data, mask)
        local f = screen:frame_number()
        if f <= 600 or (f >= 3000 and f <= 3002) then
            out:write(string.format("%s %d %06x %04x\n", tag, f, offset, data))
        end
        if f == 3003 then out:flush() end
    end)
end
sbreads_a = tap(":maincpu")
sbreads_b = tap(":slave")
