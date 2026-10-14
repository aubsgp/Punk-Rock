package.cpath = package.cpath .. ";./bin/?.dll"
local lv = require("LiquidVoice")

local Mem = require("Mem")
local MemNode = Mem.MemNode

local MAX_VOLUME = 128
local volume = 0
local muted = false
local paused = 0
local bgm_paused = {
    [0] = 0, -- field track paused
    [1] = 0  -- battle track paused
}
local hotkey_held = false
local track = {
    [0] = 0, -- field track
    [1] = 0  -- battle track
}

local context = 0

-- https://github.com/pret/pokeplatinum/blob/main/include/sound_system.h
local sSoundSystem = MemNode:root(0x02101df8) -- Base address, fixed position. If you're adapting this code for HGSS or DP, this is what you change.
local sSoundSystem_currentBGM     = sSoundSystem:child({0xbcd0c}) -- offset by 0xbcd0c
local sSoundSystem_fieldBGMPaused = sSoundSystem:child({0xbcd10}) -- offset by 0xbcd10
local sSoundSystem_bgmPaused      = sSoundSystem:child({0xbcd11}) -- offset by 0xbcd11

-- https://github.com/ntrtwl/NitroSystem/blob/main/include/nnsys/snd/player.h
-- https://github.com/ntrtwl/NitroSystem/blob/main/libraries/snd/src/player.c
local sPlayer_addr = MemNode:root(0x021cb3a4) -- Base address, fixed position. If you're adapting this code for HGSS or DP, this is what you change.
local NNSSndPlayerSize = 0x24
local sPlayer = {
    [0] = sPlayer_addr:child({NNSSndPlayerSize * 1}), -- offset by 0x24 * 1
    [1] = sPlayer_addr:child({NNSSndPlayerSize * 7})  -- offset by 0x24 * 7
}
local sPlayer_count = {
    [0] = sPlayer[0]:child({0x08}), -- offset by 0x08
    [1] = sPlayer[1]:child({0x08})  -- offset by 0x08
}
local sPlayer_volume = {
    [0] = sPlayer[0]:child({0x20}), -- offset by 0x20
    [1] = sPlayer[1]:child({0x20})  -- offset by 0x20
}

local SeqPlayer = {
    [0] = sPlayer[0]:child({0}), -- deref
    [1] = sPlayer[1]:child({0})  -- deref
}
local SeqPlayer_pauseFlag = {
    [0] = SeqPlayer[0]:child({0x2e}), -- offset by 0x2e
    [1] = SeqPlayer[1]:child({0x2e})  -- offset by 0x2e
}
local SeqPlayer_volume = {
    [0] = SeqPlayer[0]:child({0x3e}), -- offset by 0x3e
    [1] = SeqPlayer[1]:child({0x3e})  -- offset by 0x3e
}

local COMMAND_SET_PAUSE    = 0 -- Legal values: 0 or 1. 0 = play, 1 = pause.
local COMMAND_SET_MUTE     = 1 -- Legal values: 0 or 1. 0 = mute, 1 = unmuted.
local COMMAND_SET_FADE     = 2 -- Legal values: 0 to 128. Fixed-point volume setting with a denominator of 128, basically.
local COMMAND_SET_TRACK    = 3 -- Legal values: 0 to iirc 0x854. Some values in-between have no music.
local COMMAND_SET_PLAYER   = 4 -- Values do not matter.
local COMMAND_CLOSE_PLAYER = 5 -- Values do not matter. If this command is sent, close the program.

local last_command
local last_target
local last_value
local failed = false

local debug = false
local command_text = {
    [0] = "Pause",
    [1] = "Mute",
    [2] = "Fade",
    [3] = "Track",
    [4] = "Player",
    [5] = "Close!"
}
local try_write_pipe = function(command, target, value) -- A version of write_pipe where we'll try again if we fail.
    last_command = command
    last_target = target
    last_value = value
    if debug then
        print(command_text[command] .. ": " .. target .. ", " .. value)
    end
    return lv.write_pipe(command, target, value)
end

-- The SeqPlayers store the volume in decibels, or I suppose more accurately signed centibels. So, for instance, 0xfff0 = -1.6 decibels.
-- https://en.wikipedia.org/wiki/Decibel
-- https://github.com/ntrtwl/NitroSystem/blob/main/libraries/snd/src/player.c#L639
-- https://github.com/ntrtwl/NitroSDK/blob/main/include/nitro/snd/common/util.h#L43
-- https://github.com/ntrtwl/NitroSDK/blob/main/libraries/snd/src/snd_util.c#L206
local cB_to_amplitude = function(cB_read) -- 0 dB is "full volume". cB = "centibels"
    local cB = cB_read + 421 -- Because we set the sPlayer volume to 0x01, rather than its usual 0x7f, the value the game computes is 421 below what it normally is. We manually correct for that here.
    if cB >= 0x8000 then
        cB = cB - 0x10000 -- The value the game tracks is signed.
    end
    if (cB > 0) then
        return 0 -- Volume seems to un-suppress briefly when switching tracks. We just go ahead and keep that from blowing the player's ears out.
    end
    local exponent = (1/200) * cB
    return math.floor(MAX_VOLUME * (10 ^ exponent))
end

failed = not try_write_pipe(COMMAND_SET_PAUSE, 0, 0)
if failed then
    print("Communication failed!")
    print("If PunkRock.exe is not open, please open it.")
end

emu.registerafter(function()
    MemNode:clear_cache()
    sPlayer_volume[0]:writebyte(0x01) -- We can't use 0, or else the game will stop computing cB values for the SeqPlayers. But 1 is inaudible, anyway.
    sPlayer_volume[1]:writebyte(0x01)

    if failed then
        failed = not try_write_pipe(last_command, last_target, last_value)
        if not failed then
            print("Communication succeeded!")
        else
            return
        end
    end

    local new_context = nil
    if sPlayer_count[1]:readbyte() == 1 then -- bgm player takes precedence
        new_context = 1
    elseif sPlayer_count[0]:readbyte() == 1 then
        new_context = 0
        track[1] = 0
    else
        if not muted then
            muted = true
            try_write_pipe(COMMAND_SET_MUTE, 0, 0)
            track[0] = 0
            track[1] = 0
        end
        return
    end

    if muted then
        muted = false
        try_write_pipe(COMMAND_SET_MUTE, 0, 1)
    end
    if new_context ~= context then
        context = new_context
        try_write_pipe(COMMAND_SET_PLAYER, context, 0)
    end

    local volume_read = cB_to_amplitude(SeqPlayer_volume[context]:readword())
    if volume_read ~= volume then
        volume = volume_read
        try_write_pipe(COMMAND_SET_FADE, context, volume)
    end

    local curr_track = sSoundSystem_currentBGM:readword()
    if track[context] ~= curr_track and curr_track ~= 0 then
        track[context] = curr_track
        try_write_pipe(COMMAND_SET_TRACK, context, curr_track)
    end

    local keys = input.get()
    if keys and keys["backspace"] then -- Set to whatever you like.
        if not hotkey_held then
            hotkey_held = true
            paused = 1 - paused -- 0 becomes 1, 1 becomes 0
            try_write_pipe(COMMAND_SET_PAUSE, 2, paused)
        end
    else
        hotkey_held = false
    end

    local pause_read_field  = sSoundSystem_fieldBGMPaused:readbyte()
    local pause_read_battle = sSoundSystem_bgmPaused:readbyte()
    if pause_read_field ~= bgm_paused[0] then
        bgm_paused[0] = pause_read_field
        try_write_pipe(COMMAND_SET_PAUSE, 0, bgm_paused[0])
    end
    if pause_read_battle ~= bgm_paused[1] then
        bgm_paused[1] = pause_read_battle
        try_write_pipe(COMMAND_SET_PAUSE, 1, bgm_paused[1])
    end
end)

gui.register(function()
    if hotkey_held then
        if paused == 1 then
            gui.text(0, 0, "Paused!")
        else
            gui.text(0, 0, "Playing.")
        end
    end

    if debug then
        pcall(function() gui.text(0, 15, sPlayer_count[0]:readbyte()) end)
        pcall(function() gui.text(50, 15, string.format("-0x%x", 0x10000 - SeqPlayer_volume[0]:readword() - 421)) end)

        pcall(function() gui.text(0, 30, sPlayer_count[1]:readbyte()) end)
        pcall(function() gui.text(50, 30, string.format("-0x%x", 0x10000 - SeqPlayer_volume[1]:readword() - 421)) end)
    end
end)

emu.registerexit(function()
    --sPlayer_volume[0]:writebyte(0x7f)
    --sPlayer_volume[1]:writebyte(0x7f)
    lv.write_pipe(COMMAND_CLOSE_PLAYER, 0, 0)
end)
