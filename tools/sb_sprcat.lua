-- sb_sprcat.lua: the catalog of sprite images Star Blade shows, for the sprite
-- editor. Every frame the C355 list is walked as sprites.cpp does; each
-- distinct image (tile list) is kept once, with its palette bank's colors.
-- Plays a game by itself (coins, start, firing, wandering stick) after attract.
--   starblade.exe starblad -video none -sound none -nothrottle -seconds_to_run 900
--       -autoboot_delay 0 -autoboot_script sb_sprcat.lua
local screen = manager.machine.screens[":screen"]
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local OUT = os.getenv("SB_CATALOG") or "sprite_catalog.txt"

local seen = {}       -- key -> entry
local order = {}
local function sext(v, bits) local m = 1 << (bits - 1); v = v & ((1 << bits) - 1); return (v ~ m) - m end
local function word(byteaddr) return sp:read_u16(0x700000 + byteaddr) end

local function walk(f)
    local xscroll = sext(sp:read_u16(0x720002), 10)
    for i = 0, 255 do
        local which = word(0x2000 + 2 * i)
        local e = (which & 0xff) * 16
        local palette = word(e + 12)
        local link = word(e) & 0x7ff
        local offset = word(e + 2)
        local hsize, vsize = word(e + 8) & 0x3ff, word(e + 10) & 0x3ff
        local fmt = 0x4000 + link * 8
        local tileIndex = word(fmt)
        local fw = word(fmt + 2)
        local cols, rows = (fw >> 4) & 15, fw & 15
        if cols == 0 then cols = 16 end
        if rows == 0 then rows = 16 end
        if hsize > 0 and vsize > 0 and tileIndex + cols * rows <= 0x4000 then
            local key = 1469598103934665603
            local function mix(v) key = (key ~ v) * 1099511628211 end
            mix((cols << 8) | rows)
            mix(offset & 0xffff)
            local tiles = {}
            for k = 0, cols * rows - 1 do
                local t = word(0x8000 + 2 * (tileIndex + k))
                tiles[#tiles + 1] = t
                mix(t)
            end
            local ks = string.format("%016x", key)
            local ent = seen[ks]
            if not ent then
                local bank = palette & 15
                local colors = {}
                for p = 0, 255 do
                    local idx = 0x1000 + bank * 256 + p
                    local m = sp:read_u16(0x740000 + 2 * idx)
                    local b = sp:read_u16(0x750000 + 2 * idx) & 0xff
                    colors[#colors + 1] = string.format("%02x%02x%02x", m >> 8, m & 0xff, b)
                end
                ent = { key = ks, cols = cols, rows = rows, offset = offset & 0xffff, tiles = tiles,
                        bank = bank, colors = table.concat(colors), first = f, frames = 0,
                        w = hsize, h = vsize }
                seen[ks] = ent
                order[#order + 1] = ent
            end
            ent.frames = ent.frames + 1
        end
        if which & 0x100 ~= 0 then break end
    end
end

local function save()
    local fh = io.open(OUT, "w")
    fh:write("# key cols rows offset bank frames first_frame screen_w screen_h tiles... | 256 colors RRGGBB\n")
    for _, e in ipairs(order) do
        local t = {}
        for _, v in ipairs(e.tiles) do t[#t + 1] = string.format("%04x", v) end
        fh:write(string.format("%s %d %d %04x %d %d %d %d %d %s | %s\n", e.key, e.cols, e.rows, e.offset,
            e.bank, e.frames, e.first, e.w, e.h, table.concat(t, " "), e.colors))
    end
    fh:close()
end

-- Auto-play: coins and start every so often, then fire and wander.
local fields = {}
for _, port in pairs(manager.machine.ioport.ports) do
    for name, field in pairs(port.fields) do fields[name] = field end
end
local function set(name, v) local fl = fields[name]; if fl then fl:set_value(v) end end
math.randomseed(1991)
local sx, sy = 0x80, 0x80

sbcat_f = emu.add_machine_frame_notifier(function()
    local f = screen:frame_number()
    if f > 900 then walk(f) end
    local cycle = f % 3600                      -- a minute: coins + start, then play
    set("Coin 1", (cycle == 100 or cycle == 110) and 1 or 0)
    set("1 Player Start", (cycle >= 200 and cycle < 210) and 1 or 0)
    if f > 2000 then
        if f % 30 == 0 then
            sx = math.max(0x3f, math.min(0xbf, sx + math.random(-40, 40)))
            sy = math.max(0x3f, math.min(0xbf, sy + math.random(-40, 40)))
        end
        set("AD Stick X", sx); set("AD Stick Y", sy)
        set("P1 Button 2", (f % 8 < 4) and 1 or 0)          -- gun
        set("P1 Button 1", (f % 240 == 0) and 1 or 0)       -- missile
    end
    if f % 600 == 0 then save() end
end)
sbcat_stop = emu.add_machine_stop_notifier(save)
