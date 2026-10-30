-- BizHawk (melonDS core) frame-indexed capture.
--
-- Usage: load the US HG/SS ROM (determinism settings: tools/golden/README.md),
-- then run this script from Tools > Lua Console with the scene name set:
--   SCENE = "boot-title"; dofile("tools/golden/capture.lua")
-- or pass it on the command line:
--   EmuHawk --lua=tools/golden/capture.lua --lua-args... (see README)
--
-- Reads  tests/inputs/<SCENE>.txt  (format below)
-- Writes tests/golden/<SCENE>/<frame>.png  (git-ignored: shows game art)
--
-- Input script format, one event per line, '#' starts a comment:
--   every: N          capture every Nth frame (default 10)
--   end: F            stop after frame F
--   <frame> <buttons> [touch X Y]
--     <buttons> is "-" or a comma list of BizHawk NDS button names
--     (Up Down Left Right A B X Y L R Start Select).
--     "touch X Y" holds a stylus press at X,Y (0-255, 0-191) on the lower screen.
--   The state set on a line is held until the next line.
--
-- UNVERIFIED against a running BizHawk: the names of the touch inputs. The
-- script prints joypad.get() on frame 0 so you can check them, and the names
-- are configurable below.

local SCENE = SCENE or "boot-title"
local TOUCH_BUTTON, TOUCH_X, TOUCH_Y = "Touch", "Touch X", "Touch Y"

local function parse(path)
    local f = assert(io.open(path, "r"), "cannot open " .. path)
    local ev, every, last = {}, 10, nil
    for line in f:lines() do
        line = line:gsub("#.*", ""):match("^%s*(.-)%s*$")
        if line ~= "" then
            local n = line:match("^every:%s*(%d+)$")
            local e = line:match("^end:%s*(%d+)$")
            if n then every = tonumber(n)
            elseif e then last = tonumber(e)
            else
                local frame, rest = line:match("^(%d+)%s+(.*)$")
                assert(frame, "bad line: " .. line)
                local buttons, tx, ty = rest:match("^(%S+)%s+touch%s+(%d+)%s+(%d+)$")
                if not buttons then buttons = rest:match("^(%S+)$") end
                assert(buttons, "bad line: " .. line)
                local set = {}
                if buttons ~= "-" then
                    for b in buttons:gmatch("[^,]+") do set[b] = true end
                end
                ev[#ev + 1] = { frame = tonumber(frame), buttons = set,
                                tx = tonumber(tx), ty = tonumber(ty) }
            end
        end
    end
    f:close()
    assert(last, "script needs an 'end: F' line")
    return ev, every, last
end

local events, every, last = parse("tests/inputs/" .. SCENE .. ".txt")
local outdir = "tests/golden/" .. SCENE
os.execute('mkdir -p "' .. outdir .. '"')

print("joypad at start: ")
for k, v in pairs(joypad.get()) do print("  ", k, v) end

local idx, cur = 1, nil
while emu.framecount() <= last do
    local fc = emu.framecount()
    while events[idx] and events[idx].frame <= fc do cur = events[idx]; idx = idx + 1 end
    if cur then
        local pad = {}
        for b in pairs(cur.buttons) do pad[b] = true end
        if cur.tx then
            pad[TOUCH_BUTTON] = true
            joypad.setanalog({ [TOUCH_X] = cur.tx, [TOUCH_Y] = cur.ty })
        end
        joypad.set(pad)
    end
    if fc % every == 0 then
        client.screenshot(string.format("%s/%06d.png", outdir, fc))
    end
    emu.frameadvance()
end
print("done: " .. SCENE)
client.exit()
