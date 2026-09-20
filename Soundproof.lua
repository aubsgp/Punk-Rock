package.cpath = package.cpath .. ";./bin/?.dll"
local lv = require("LiquidVoice")

local muted = false
local paused = false
local hotkey_held = false
local track = {
    [0] = 0, -- field track
    [1] = 0  -- battle track
}

local BATTLE_ADDR = 0x02197910
local in_battle = memory.readdword(BATTLE_ADDR)
local left_battle = false

-- https://github.com/pret/pokeplatinum/blob/main/include/sound_system.h
local sSoundSystem_fadeCounter = 0x021beaf8
local sSoundSystem_currentBGM  = 0x021beb04
local fade = 0 -- Negative = fading in. Positive = fading out. Denominator = |fade|. See code below.
local fade_read = 0
local fade_val = 0 -- Numerator of fade.

-- https://github.com/ntrtwl/NitroSystem/blob/main/include/nnsys/snd/player.h
-- https://github.com/ntrtwl/NitroSystem/blob/main/libraries/snd/src/player.c
local sPlayer = 0x021cb3a4
local volumeOffset = 0x20
local NNSSndPlayerSize = 0x24
local PLAYER_FIELD = 1
local PLAYER_BGM = 7

local COMMAND_PAUSE            = 0 -- Legal values: 0 or 1
local COMMAND_SET_MUTE         = 1 -- Legal values: 0 or 1
local COMMAND_SET_FADE         = 2 -- Legal values: 0 to 128. Fixed-point, basically.
local COMMAND_SET_TRACK_FIELD  = 3 -- Legal values: 0 to iirc 0x854. Some values in-between have no music.
local COMMAND_SET_TRACK_BATTLE = 4 -- Legal values: 0 to iirc 0x854. Some values in-between have no music.
local COMMAND_SET_PLAYER       = 5 -- Legal values: 0 (field) or 1 (battle). Switches without setting a track, so it'll resume any music that was playing.
local COMMAND_CLOSE_PLAYER     = 6 -- Values do not matter. If this command is sent, close the program.

local last_command
local last_value
local failed = false

local try_write_pipe = function(command, value) -- A version of write_pipe where we'll try again if we fail.
    last_command = command
    last_value = value
    return lv.write_pipe(last_command, last_value)
end

local set_volume = function(player, volume)
    local addr = sPlayer + (player * NNSSndPlayerSize) + volumeOffset
    memory.writedword(addr, volume)
end

emu.registerafter(function()
    set_volume(PLAYER_FIELD, 0)
    set_volume(PLAYER_BGM, 0)

    if failed then
        failed = not try_write_pipe(last_command, last_value)
        if not failed then
            print("Communication succeeded!")
        end
    end

    local battle_read = memory.readbyte(BATTLE_ADDR)
    if battle_read ~= in_battle then
        in_battle = battle_read
        if in_battle == 0 then
            track[1] = 0
            lv.write_pipe(COMMAND_SET_PLAYER, 0)
            left_battle = true
        end
    end

    fade_read = memory.readbyte(sSoundSystem_fadeCounter)
    if fade_read ~= 0 and fade == 0 then
        if left_battle then
            left_battle = false
            fade = -1*fade_read
        else
            fade = fade_read
        end
    elseif fade_read ~= 0 and fade ~= 0 then
        if fade > 0 then
            fade_val = math.floor(128 * fade_read / fade)
        else
            fade_val = math.floor(128 * (fade_read + fade) / fade)
        end
        lv.write_pipe(COMMAND_SET_FADE, fade_val)
    else
        fade = 0
        if not left_battle and in_battle ~= 1 and fade_val ~= 128 then
            fade_val = 128
            lv.write_pipe(COMMAND_SET_FADE, fade_val)
        end
    end

    local curr_track = memory.readword(sSoundSystem_currentBGM)
    if track[in_battle] ~= curr_track and curr_track ~= 0 then
        track[in_battle] = curr_track
        failed = not try_write_pipe(COMMAND_SET_TRACK_FIELD + in_battle, curr_track)
        if failed then
            print("Communication failed!")
            print("If PunkRock.exe is not open, please open it.")
            return
        end
    end

    local keys = input.get()

    if keys and keys["backspace"] then -- Set to whatever you like.
        if not hotkey_held then
            hotkey_held = true
            if paused then
                paused = false;
                lv.write_pipe(COMMAND_PAUSE, 0)
            else
                paused = true;
                lv.write_pipe(COMMAND_PAUSE, 1)
            end
        end
    else
        hotkey_held = false
    end
end)

gui.register(function()
    if hotkey_held then
        if paused then
            gui.text(0, 0, "Paused!")
        else
            gui.text(0, 0, "Playing.")
        end
    end
end)

emu.registerexit(function()
    set_volume(PLAYER_FIELD, 0x7f)
    set_volume(PLAYER_BGM,   0x7f)
    lv.write_pipe(COMMAND_CLOSE_PLAYER, 0)
end)
