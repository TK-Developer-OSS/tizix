-- mamezx.lua - MAME autoboot script for tizix on the ZX Spectrum +3 (MAME's specpls3).
--
-- Boot: MAME's +3 ROM Loader does not boot from the disk, so this does what it would do: at frame 150 it puts the boot
-- sector (track 0, sector 1 of the DSK named by TZ_ZXBOOT) at 0xFE00 in all-RAM configuration 3 (banks 4, 7, 6, 3) and
-- runs it at 0xFE10.
--
-- Keys: MAME maps host keys one to one onto Spectrum keys, and in its natural keyboard mode it has no way to type
-- | { } [ ] ~ \ (the +3 ROM's E-mode symbols).  So this script takes the host keyboard over: MAME's own bindings of the
-- Spectrum keys are cleared, and every host key press becomes the character it gives on the host keyboard
-- (TZ_KBD = jp, the default, or us), which is typed as the Spectrum keys that give it in tizix (console.c: Symbol Shift
-- + key = the symbol on its keycap, Caps Shift + letter = capital).  Enter, Backspace (Caps Shift + 0), Esc (Caps
-- Shift + Space, BREAK), the arrow keys (Caps Shift + 5 6 7 8) and Ctrl + letter (both shifts + the letter; Ctrl + C is
-- Break) work too.  Alt + a key is Symbol Shift + that key (the keycap symbol), for keys MAME cannot see (the Japanese
-- keyboard's yen and ro keys: | is Alt + S, \ is Alt + D, _ is Alt + 0).  Each character is held until tizix has
-- scanned the keyboard with it down, then released until it has seen it up (see type_tick), one at a time, so none is
-- lost even while the disk code runs with the interrupts off (no key repeat).
--
-- Test mode (TZ_LINES = a file): after the [/root]# prompt, types each line of the file ("\e" = Esc, "\r" = Enter in
-- the middle, "\w" = wait 3 seconds, "\L" = the left arrow key) and Enter, waits for the prompt, and prints the screen (the 32 x 24 text shadow at
-- 0xF400) between
-- "@@SCREEN <n>" and "@@END".  TZ_WAIT = frames to wait after a line before looking for the prompt (default 25).
-- Exits MAME when done or after TZ_TIMEOUT frames (default 30000).
local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local sp = cpu.spaces["program"]
local inp = machine.input

-- ---------------------------------------------------------------- Spectrum keys
local ROWS = { "\0zxcv", "asdfg", "qwert", "12345", "09876", "poiuy", "\rlkjh", " \0mnb" }
local SYMS = { "\0:`?/", "~|\\{}", "\0\0\0<>", "!@#$%", "_)('&", "\";\0][", "\r=+-^", " \0.,*" }
local CS, SS = { 0, 0 }, { 7, 1 }
local field = {}                 -- field[line][bit]
for line = 0, 7 do
  field[line] = {}
  for _, f in pairs(machine.ioport.ports[":LINE" .. line].fields) do
    local bit = 0
    while (1 << bit) ~= f.mask do bit = bit + 1 end
    field[line][bit] = f
  end
end

local function find(t, ch)
  for r = 1, 8 do
    local i = t[r]:find(ch, 1, true)
    if i and ch ~= "\0" then return { r - 1, i - 1 } end
  end
end

-- the keys to hold together for one character (nil: no way to type it)
local function keys_for(ch)
  local o = ch:byte()
  if ch == "\r" or ch == "\n" then return { { 6, 0 } } end
  if o == 8 or o == 127 then return { CS, { 4, 0 } } end
  if o == 27 then return { CS, { 7, 0 } } end
  if o >= 1 and o <= 26 then return { CS, SS, find(ROWS, string.char(o + 96)) } end
  if ch:match("%u") then return { CS, find(ROWS, ch:lower()) } end
  local k = find(ROWS, ch)
  if k then return { k } end
  k = find(SYMS, ch)
  if k then return { SS, k } end
end

-- ---------------------------------------------------------------- typing queue
-- A key is held until tizix has scanned the keyboard with it down (the last half-row, port 0x7FFE, is read at the end
-- of every scan: console.c kb_scan) and released until it has scanned it up, so nothing is lost while the disk code
-- runs with the interrupts off (no scan then).  At least 2 frames each way; at most 100 (a scan that never comes, e.g.
-- before tizix runs).
local scans = 0
scantap = cpu.spaces["io"]:install_read_tap(0x7ffe, 0x7ffe, "zxscan", function(off, data, mask)
  scans = scans + 1
  return data
end)
local queue, held, phase, mark, frames = {}, nil, "idle", 0, 0
local function press(keys, on)
  for _, k in ipairs(keys) do
    local f = field[k[1]][k[2]]
    if on then f:set_value(1) else f:clear_value() end
  end
end
local function type_tick()
  frames = frames + 1
  if phase == "pause" then
    if frames >= mark then phase = "idle" end
    return
  end
  if phase ~= "idle" and frames >= 2 and (scans > mark or frames >= 100) then
    if phase == "down" then
      press(held, false)
      held = nil
      phase, mark, frames = "up", scans, 0
      return
    end
    phase = "idle"
  end
  if phase ~= "idle" then return end
  local keys = table.remove(queue, 1)
  if type(keys) == "number" then
    phase, mark, frames = "pause", keys, 0
  elseif keys then
    held = keys
    press(keys, true)
    phase, mark, frames = "down", scans, 0
  end
end
local function busy() return #queue > 0 or phase ~= "idle" end
local function type_text(s)
  for c in s:gmatch(".") do
    local k = keys_for(c)
    if k then queue[#queue + 1] = k end
  end
end
local ARROW = { up = { CS, { 4, 3 } }, down = { CS, { 4, 4 } }, right = { CS, { 4, 2 } }, left = { CS, { 3, 4 } } }

-- ---------------------------------------------------------------- host keyboard
-- host key -> { unshifted, shifted } characters
local layout = {}
for c in ("abcdefghijklmnopqrstuvwxyz"):gmatch(".") do layout["KEYCODE_" .. c:upper()] = { c, c:upper() } end
local digits_jp = { "!", "\"", "#", "$", "%", "&", "'", "(", ")", "" }
local digits_us = { "!", "@", "#", "$", "%", "^", "&", "*", "(", ")" }
local jp = (os.getenv("TZ_KBD") or "jp") ~= "us"
for d = 1, 10 do
  local c = tostring(d % 10)
  layout["KEYCODE_" .. c] = { c, (jp and digits_jp or digits_us)[d] }
end
local punct = jp and {
  KEYCODE_MINUS = { "-", "=" }, KEYCODE_EQUALS = { "^", "~" }, KEYCODE_OPENBRACE = { "@", "`" },
  KEYCODE_CLOSEBRACE = { "[", "{" }, KEYCODE_BACKSLASH = { "]", "}" }, KEYCODE_COLON = { ";", "+" },
  KEYCODE_QUOTE = { ":", "*" }, KEYCODE_COMMA = { ",", "<" }, KEYCODE_STOP = { ".", ">" }, KEYCODE_SLASH = { "/", "?" },
  KEYCODE_BACKSLASH2 = { "\\", "_" }, KEYCODE_SPACE = { " ", " " },
} or {
  KEYCODE_MINUS = { "-", "_" }, KEYCODE_EQUALS = { "=", "+" }, KEYCODE_OPENBRACE = { "[", "{" },
  KEYCODE_CLOSEBRACE = { "]", "}" }, KEYCODE_BACKSLASH = { "\\", "|" }, KEYCODE_COLON = { ";", ":" },
  KEYCODE_QUOTE = { "'", "\"" }, KEYCODE_COMMA = { ",", "<" }, KEYCODE_STOP = { ".", ">" }, KEYCODE_SLASH = { "/", "?" },
  KEYCODE_TILDE = { "`", "~" }, KEYCODE_SPACE = { " ", " " },
}
for k, v in pairs(punct) do layout[k] = v end
local special = { KEYCODE_ENTER = "\r", KEYCODE_ENTER_PAD = "\r", KEYCODE_BACKSPACE = "\8", KEYCODE_ESC = "\27" }
local arrows = { KEYCODE_UP = "up", KEYCODE_DOWN = "down", KEYCODE_LEFT = "left", KEYCODE_RIGHT = "right" }

local code, was = {}, {}
local function c(t) if not code[t] then code[t] = inp:code_from_token(t) end return code[t] end
local function down(t) return inp:code_pressed(c(t)) end

local function host_tick()
  local shift = down("KEYCODE_LSHIFT") or down("KEYCODE_RSHIFT")
  local ctrl = down("KEYCODE_LCONTROL") or down("KEYCODE_RCONTROL")
  local alt = down("KEYCODE_LALT") or down("KEYCODE_RALT")
  local function edge(t)
    local d = down(t)
    local e = d and not was[t]
    was[t] = d
    return e
  end
  for t, v in pairs(layout) do
    if edge(t) then
      if alt then
        -- Symbol Shift + the Spectrum key at this place (letters, digits, space)
        local k = find(ROWS, v[1])
        if k then queue[#queue + 1] = { SS, k } end
      elseif ctrl and v[1]:match("%l") then
        type_text(string.char(v[1]:byte() - 96))
      else
        type_text(shift and v[2] or v[1])
      end
    end
  end
  for t, s in pairs(special) do if edge(t) then type_text(s) end end
  for t, a in pairs(arrows) do if edge(t) then queue[#queue + 1] = ARROW[a] end end
end

-- MAME's own bindings would press keys too (host Shift = Caps Shift, and so on): clear them
local none = inp:seq_from_tokens("")
for line = 0, 7 do
  for _, f in pairs(field[line]) do f:set_input_seq("standard", none) end
end
for _, tag in ipairs({ ":PLUS0", ":PLUS1" }) do
  local p = machine.ioport.ports[tag]
  if p then for _, f in pairs(p.fields) do f:set_input_seq("standard", none) end end
end

-- ---------------------------------------------------------------- boot, test mode
local function zxboot(path)
  local fh = assert(io.open(path, "rb"))
  local d = fh:read("a")
  fh:close()
  cpu.spaces["io"]:write_u8(0x1ffd, 0x07)
  for i = 0, 511 do sp:write_u8(0xFE00 + i, d:byte(0x200 + i + 1)) end
  cpu.state["PC"].value = 0xFE10
end

local function screen()
  local rows = {}
  for r = 0, 23 do
    local t = {}
    for col = 0, 31 do
      local b = sp:read_u8(0xF400 + r * 32 + col)
      if b < 32 or b > 126 then b = 46 end
      t[#t + 1] = string.char(b)
    end
    rows[#rows + 1] = (table.concat(t):gsub("%s+$", ""))
  end
  return (table.concat(rows, "\n"):gsub("%s+$", ""))
end
local function prompt() return screen():sub(-2) == "]#" end

local lines
if os.getenv("TZ_LINES") then
  lines = {}
  for l in io.lines(os.getenv("TZ_LINES")) do lines[#lines + 1] = l end
end
local limit = tonumber(os.getenv("TZ_TIMEOUT") or "30000")
local wait = tonumber(os.getenv("TZ_WAIT") or "25")
local f, idx, since = 0, 0, 0

emu.register_frame_done(function()
  f = f + 1
  since = since + 1
  if f == 150 and os.getenv("TZ_ZXBOOT") then zxboot(os.getenv("TZ_ZXBOOT")) end
  if not lines then
    host_tick()
  elseif f > limit then
    print("@@SCREEN timeout") print(screen()) print("@@END")
    machine:exit()
    return
  elseif f > 150 and not busy() and since > wait and f % 10 == 0 and prompt() then
    print("@@SCREEN " .. idx) print(screen()) print("@@END")
    idx = idx + 1
    if idx > #lines then machine:exit() return end
    for part, w in (lines[idx] .. "\\n"):gmatch("(.-)\\([wLn])") do
      type_text((part:gsub("\\e", "\27"):gsub("\\r", "\r")))
      if w == "w" then queue[#queue + 1] = 150 elseif w == "L" then queue[#queue + 1] = ARROW.left end
    end
    type_text("\r")
    since = 0
  end
  type_tick()
end)
