-- sb_snap.lua: MAME's own picture at the captured burst frames.
local screen = manager.machine.screens[":screen"]
local want = { [3001]=true, [3002]=true, [9001]=true, [9002]=true, [6000]=true }
local function onFrame()
    local f = screen:frame_number()
    if want[f] then screen:snapshot(string.format("sbsnap_%06d.png", f)) end
end
if emu.add_machine_frame_notifier then
    sbsnap_frame = emu.add_machine_frame_notifier(onFrame)
else
    emu.register_frame_done(onFrame)
end
