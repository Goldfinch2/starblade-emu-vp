-- sb_hle.lua: the DSP board replaced by a stand-in. The five DSPs are halted;
-- this script answers the 68000's handshake the way the DSP board does and
-- logs the frame pages, to prove the game runs without them.
--   starblade.exe starblad -debug -debugger none -autoboot_script sb_hle.lua
local dbg = manager.machine.debugger
dbg:command("suspend :namcos21dsp_c67:dspmaster,:namcos21dsp_c67:dspslave0,:namcos21dsp_c67:dspslave1,:namcos21dsp_c67:dspslave2,:namcos21dsp_c67:dspslave3")
dbg:command("go")

local main = manager.machine.devices[":maincpu"].spaces["program"]
local screen = manager.machine.screens[":screen"]
local log = io.open("sbhle.txt", "w")

local function r(a) return main:read_u16(a) end
local function w(a, v) main:write_u16(a, v) end

local booted = false
local function onFrame()
    local f = screen:frame_number()
    if r(0x200112) ~= 0 then
        w(0x200112, 0)
        w(0x20010a, 0xee52); w(0x20010c, 0xd5df)
        w(0x200024, 0x4545); w(0x200026, 0x3532)    -- "EE52"
        w(0x200110, 1)
        log:write(string.format("%d boot: 0x200112 answered\n", f))
    end
    if r(0x20010e) ~= 0 then
        w(0x20010e, 0)
        log:write(string.format("%d boot: upload 0x20010e answered\n", f))
    end
    if r(0x200202) == 0xffff then
        w(0x200202, 0)
        booted = true
        log:write(string.format("%d boot: 0x200202 answered, running\n", f))
    end
    if booted and f % 300 == 0 then
        local page = r(0x200206)
        local base = 0x208200 + (page & 1) * 0x4000
        local n, a = 0, base
        while r(a) ~= 0xffff and n < 300 do
            local t = r(a)
            a = a + 2 * ((t == 0) and 6 or ((t == 0x100) and 11 or ((t == 0x101) and 14 or 13)))
            n = n + 1
        end
        log:write(string.format("%d page %d: %d records\n", f, page, n))
        log:flush()
    end
    if f == 1500 or f == 3000 or f == 6000 or f == 9000 then
        screen:snapshot(string.format("sbhle_%06d.png", f))
    end
end
sbhle_frame = emu.add_machine_frame_notifier(onFrame)
