-- sb_dump.lua: Star Blade (namcos21 C67) capture, for re-creating what its DSPs do.
--   mame.exe starblad -window -autoboot_script sb_dump.lua
-- Let it run through the attract mode (about 5 minutes; the demo plays the
-- game), then quit. Writes into the current folder:
--   sbdump_dspram_<frame>.bin   shared DSP RAM, 64 KB as the 68000 sees it
--                               (0x200000-0x20ffff): handshake, frame control,
--                               camera, object list, the master's slave lists
--   sbdump_writes.txt           the 68000's writes into it during the bursts:
--                               frame, address, value
--   sbdump_master_prog.bin      the master DSP's program RAM (0x8000-0xbfff, words)
--   sbdump_slave_prog.bin       the slave DSP's program RAM (0x8000-0x8fff, words)
--   sbdump_point24.bin          the point ROMs (24-bit points in 32-bit words, big-endian)

local main = manager.machine.devices[":maincpu"].spaces["program"]
local master = manager.machine.devices[":namcos21dsp_c67:dspmaster"].spaces["program"]
local slave = manager.machine.devices[":namcos21dsp_c67:dspslave0"].spaces["program"]
local screen = manager.machine.screens[":screen"]

local function burst(f)
    return (f >= 3000 and f < 3010) or (f >= 9000 and f < 9010)
end

local function snapshot(f)
    local out = {}
    for a = 0x200000, 0x20ffff, 2 do
        local w = main:read_u16(a)
        out[#out + 1] = string.char(w >> 8, w & 0xff)
    end
    local fh = io.open(string.format("sbdump_dspram_%06d.bin", f), "wb")
    fh:write(table.concat(out))
    fh:close()
end

-- DSP spaces are word-addressed: one address per 16-bit word.
local function dumpWords(space, first, last, name)
    local out = {}
    for a = first, last do
        local w = space:read_u16(a)
        out[#out + 1] = string.char(w >> 8, w & 0xff)
    end
    local fh = io.open(name, "wb")
    fh:write(table.concat(out))
    fh:close()
end

local function dumpRegion(tag, name)
    local r = manager.machine.memory.regions[tag]
    if not r then return end
    local out = {}
    for i = 0, r.size - 1 do
        out[#out + 1] = string.char(r:read_u8(i))
    end
    local fh = io.open(name, "wb")
    fh:write(table.concat(out))
    fh:close()
end

local writes = io.open("sbdump_writes.txt", "w")
local tap = main:install_write_tap(0x200000, 0x20ffff, "sbdump", function(offset, data, mask)
    local f = screen:frame_number()
    if burst(f) then
        writes:write(string.format("%d %06x %04x %04x\n", f, offset, data, mask))
    end
end)

local done = {}
local function onFrame()
    local f = screen:frame_number()
    if (f >= 600 and f % 300 == 0) or burst(f) then
        snapshot(f)
    end
    if f == 3000 and not done.prog then
        done.prog = true
        dumpWords(master, 0x8000, 0xbfff, "sbdump_master_prog.bin")
        dumpWords(slave, 0x8000, 0x8fff, "sbdump_slave_prog.bin")
        dumpRegion(":namcos21dsp_c67:point24", "sbdump_point24.bin")
        writes:flush()
        print("sb_dump: programs and point ROMs saved")
    end
end

if emu.add_machine_frame_notifier then
    sbdump_frame = emu.add_machine_frame_notifier(onFrame)
else
    emu.register_frame_done(onFrame)
end
sbdump_tap = tap        -- keep the tap alive
print("sb_dump: capturing")
