-- sb_order.lua: both 68000s' writes to the DSP RAM pages and the page select, in order.
local screen = manager.machine.screens[":screen"]
local out = io.open("sborder.txt", "w")
local want = function(f) return (f >= 1795 and f <= 1798) or (f >= 3000 and f <= 3002) or (f >= 1495 and f <= 1497) end
local function tap(tag)
  local sp = manager.machine.devices[tag].spaces["program"]
  return sp:install_write_tap(0x200200, 0x20ffff, "o" .. tag, function(o, d)
    local f = screen:frame_number()
    if want(f) then out:write(string.format("%d %s %06x %04x %d\n", f, tag, o, d, 0)) end
  end)
end
sbo1 = tap(":maincpu"); sbo2 = tap(":slave")
sbo_f = emu.add_machine_frame_notifier(function() if screen:frame_number() == 3003 then out:close(); manager.machine:exit() end end)
