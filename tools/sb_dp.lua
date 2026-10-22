-- sb_dp.lua: traffic in the dual-port RAM (68000s, sound 6809, C68 MCU), change-only,
-- through boot, a coin, a start and some play.
local screen = manager.machine.screens[":screen"]
local out = io.open("sbdp.txt", "w")
local last = {}
local function log(kind, tag, off, data)
    local key = kind .. tag .. off
    if last[key] ~= data then
        last[key] = data
        out:write(string.format("%s %s %d %04x %02x\n", kind, tag, screen:frame_number(), off, data & 0xff))
    end
end
local function taps(tag, first, last_, step)
    local sp = manager.machine.devices[tag].spaces["program"]
    return {
        sp:install_read_tap(first, last_, "dpr" .. tag, function(o, d, m) log("R", tag, (o - first) // step, d) end),
        sp:install_write_tap(first, last_, "dpw" .. tag, function(o, d, m) log("W", tag, (o - first) // step, d) end),
    }
end
sbdp_t = { taps(":maincpu", 0xa00000, 0xa00fff, 2), taps(":slave", 0xa00000, 0xa00fff, 2),
           taps(":audiocpu", 0x7000, 0x77ff, 1), taps(":c68mcu:mcu", 0x5000, 0x57ff, 1) }
local fields = {}
for pname, port in pairs(manager.machine.ioport.ports) do
    for fname, field in pairs(port.fields) do
        fields[fname] = field
        out:write("# field " .. pname .. " / " .. fname .. "\n")
    end
end
local press = { [2000]="Coin 1", [2200]="1 Player Start", [2600]="P1 Button 2", [3000]="P1 Button 1", [3400]="P1 Button 2" }
local held
sbdp_f = emu.add_machine_frame_notifier(function()
    local f = screen:frame_number()
    if held then held:set_value(0); held = nil end
    if press[f] and fields[press[f]] then
        held = fields[press[f]]; held:set_value(1)
        out:write(string.format("# %d press %s\n", f, press[f]))
    end
    if f % 600 == 0 then out:flush() end
end)
