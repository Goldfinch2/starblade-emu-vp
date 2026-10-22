-- sb_slave.lua: the slave DSP's output (port 0 writes) for frames 3000-3003.
local screen = manager.machine.screens[":screen"]
local io_ = manager.machine.devices[":namcos21dsp_c67:dspslave0"].spaces["io"]
local out = io.open("sbslave.txt", "w")
sbslave_tap = io_:install_write_tap(0, 0, "sbslave", function(offset, data, mask)
    local f = screen:frame_number()
    if f >= 3000 and f <= 3003 then out:write(string.format("%d %d\n", f, data)) end
    if f == 3004 then out:flush() end
end)
