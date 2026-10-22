-- sb_cont.lua: how the MCU charges coins for a start and a continue.
local screen = manager.machine.screens[":screen"]
local out = io.open("sbcont.txt", "w")
local mcusp = manager.machine.devices[":c68mcu:mcu"].spaces["program"]
local mainsp = manager.machine.devices[":maincpu"].spaces["program"]
local slavesp = manager.machine.devices[":slave"].spaces["program"]
local watch = { [0x19]=1, [0x1b]=1, [0x18]=1, [0x1a]=1, [0x46]=1, [0x47]=1, [0x56]=1, [0x57]=1, [0x06]=1, [0x07]=1, [0x44]=1, [0x35]=1 }
local function wt(sp, tag, base)
    return sp:install_write_tap(base, base + 0xfff, "w" .. tag, function(o, d)
        local off = (o - base) // ((tag == "mcu") and 1 or 2)
        if watch[off] then out:write(string.format("%d %s W %03x %02x\n", screen:frame_number(), tag, off, d & 0xff)) end
    end)
end
t1 = wt(mcusp, "mcu", 0x5000); t2 = wt(mainsp, "main", 0xa00000); t3 = wt(slavesp, "slave", 0xa00000)
local fields = {}
for pname, port in pairs(manager.machine.ioport.ports) do
    for fname, field in pairs(port.fields) do fields[fname] = field end
end
local plan = { {"Coin 1", 2000, 3}, {"Coin 1", 2010, 3}, {"1 Player Start", 2100, 10} }
for f = 6000, 20000, 900 do
    plan[#plan + 1] = {"Coin 1", f, 3}
    plan[#plan + 1] = {"1 Player Start", f + 60, 10}
end
local shots = {}
ff = emu.add_machine_frame_notifier(function()
    local f = screen:frame_number()
    for _, p in ipairs(plan) do
        if f == p[2] then fields[p[1]]:set_value(1); out:write(string.format("# %d %s\n", f, p[1]))
            if p[1] == "Coin 1" and f >= 6000 then screen:snapshot(string.format("sbcont_%06d.png", f)) end end
        if f == p[2] + p[3] then fields[p[1]]:clear_value() end
    end
    if f % 300 == 0 then out:flush() end
end)
