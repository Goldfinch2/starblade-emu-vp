local screen = manager.machine.screens[":screen"]
sbseq_f = emu.add_machine_frame_notifier(function()
  local f = screen:frame_number()
  if f >= 900 and f % 60 == 0 then screen:snapshot(string.format("sbseq_%05d.png", f)) end
  if f >= 12000 then manager.machine:exit() end
end)
