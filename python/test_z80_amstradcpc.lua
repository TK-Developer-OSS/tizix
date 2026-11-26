-- test_z80_amstradcpc.lua - MAME autoboot script for python/test_z80_amstradcpc.py (Amstrad CPC 6128).
-- Types |CPM at the BASIC prompt, waits for the tizix prompt, then types each line of the file named by TZ_LINES,
-- waiting for the prompt after each, and prints the screen (the text shadow at 0xF400, 80 x 25) between "@@SCREEN <n>"
-- and "@@END" lines.  TZ_SNAP set: a PNG snapshot at the end.  Exits MAME when done or after TZ_TIMEOUT frames.
local lines = {}
for l in io.lines(os.getenv("TZ_LINES")) do lines[#lines + 1] = l end
local limit = tonumber(os.getenv("TZ_TIMEOUT") or "20000")
local cpu = manager.machine.devices[":maincpu"]
local sp = cpu.spaces["program"]
local kb = manager.machine.natkeyboard
local f, state, idx, since = 0, "boot", 0, 0

local function screen()
  local rows = {}
  for r = 0, 24 do
    local t = {}
    for c = 0, 79 do
      local b = sp:read_u8(0xF400 + r * 80 + c)
      if b < 32 or b > 126 then b = 46 end
      t[#t + 1] = string.char(b)
    end
    rows[#rows + 1] = (table.concat(t):gsub("%s+$", ""))
  end
  return (table.concat(rows, "\n"):gsub("%s+$", ""))
end

local function prompt() return screen():sub(-2) == "]#" end

local function dump(n)
  print("@@SCREEN " .. n)
  print(screen())
  print("@@END")
end

emu.register_frame_done(function()
  f = f + 1
  since = since + 1
  if f > limit then dump("timeout") manager.machine:exit() return end
  if state == "boot" then
    if f == 150 then kb:post("|cpm\n") state = "wait" since = 0 end
  elseif state == "wait" then
    -- the prompt, the keys all delivered, and a few frames of quiet
    if not kb.is_posting and since > 25 and f % 10 == 0 and prompt() then
      dump(idx)
      idx = idx + 1
      if idx > #lines then
        if os.getenv("TZ_SNAP") then manager.machine.video:snapshot() end
        state = "done"
        since = 0
      else
        kb:post(lines[idx] .. "\n")
        since = 0
      end
    end
  elseif state == "done" and since > 5 then
    manager.machine:exit()
  end
end)
