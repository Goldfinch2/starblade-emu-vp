-- sb_snd.lua: every write to the sound slots (0x100-0x4ff of the dual-port RAM), not change-only.
local screen = manager.machine.screens[":screen"]
local out = io.open("sbsnd.txt", "w")
local function wt(tag, base, step)
    local sp = manager.machine.devices[tag].spaces["program"]
    return sp:install_write_tap(base + 0x100 * step, base + 0x4ff * step + step - 1, "s" .. tag, function(o, d)
        local f = screen:frame_number()
        if f >= 900 and f < 5000 then out:write(string.format("%d %s %03x %02x\n", f, tag, (o - base) // step, d & 0xff)) end
    end)
end
t1 = wt(":maincpu", 0xa00000, 2); t2 = wt(":slave", 0xa00000, 2); t3 = wt(":audiocpu", 0x7000, 1)
ff = emu.add_machine_frame_notifier(function() if screen:frame_number() == 5000 then out:close() end end)
