-- sb_mcu.lua: what the C68 MCU puts in the dual-port RAM for each input.
local screen = manager.machine.screens[":screen"]
local out = io.open("sbmcu.txt", "w")
local mainsp = manager.machine.devices[":maincpu"].spaces["program"]
local slavesp = manager.machine.devices[":slave"].spaces["program"]
local mcusp = manager.machine.devices[":c68mcu:mcu"].spaces["program"]
local skip = { [0x1f]=1, [0x21]=1, [0x23]=1, [0x36]=1, [0x39]=1, [0x27]=1, [0x2b]=1, [0x2f]=1, [0x33]=1, [0x43]=1 }
local last = {}
local fullframe = { [2400]=true, [5000]=true }
t1 = mcusp:install_write_tap(0x5000, 0x57ff, "mw", function(o, d)
    local off = o - 0x5000
    if skip[off] then return end
    if last[off] ~= d then last[off] = d
        out:write(string.format("%d MCU W %03x %02x\n", screen:frame_number(), off, d)) end
end)
local function rtap(sp, tag)
    return sp:install_read_tap(0xa00000, 0xa00fff, "r" .. tag, function(o, d)
        local f = screen:frame_number()
        if fullframe[f] then out:write(string.format("%d %s R %03x %02x\n", f, tag, (o - 0xa00000) // 2, d & 0xff)) end
    end)
end
t2 = rtap(mainsp, "main"); t3 = rtap(slavesp, "slave")
local fields = {}
for pname, port in pairs(manager.machine.ioport.ports) do
    for fname, field in pairs(port.fields) do fields[fname] = field end
end
-- name, start frame, frames held, value
local plan = {
    {"Coin 1", 2000, 3, 1}, {"Coin 1", 2010, 3, 1}, {"1 Player Start", 2100, 30, 1},
    {"P1 Button 1", 3000, 120, 1}, {"P1 Button 2", 3300, 120, 1}, {"P1 Button 3", 3600, 60, 1},
    {"AD Stick X", 3800, 60, 0x3f}, {"AD Stick X", 3900, 60, 0xbf},
    {"AD Stick Y", 4000, 60, 0x3f}, {"AD Stick Y", 4100, 60, 0xbf},
    {"Service Button", 4300, 10, 1}, {"Coin 2", 4400, 3, 1}, {"Service 1", 4500, 3, 1},
}
ff = emu.add_machine_frame_notifier(function()
    local f = screen:frame_number()
    for _, p in ipairs(plan) do
        local field = fields[p[1]]
        if field then
            if f == p[2] then field:set_value(p[4]); out:write(string.format("# %d set %s %x\n", f, p[1], p[4])) end
            if f == p[2] + p[3] then field:clear_value(); out:write(string.format("# %d release %s\n", f, p[1])) end
        end
    end
    if f % 300 == 0 then out:flush() end
end)
