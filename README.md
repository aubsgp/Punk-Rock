Installation
========
1) Go [here](https://github.com/aubsgp/Punk-Rock/releases) for releases.
2) Extract the contents of the .zip into your Desmume folder. "lua51.dll" should sit next to your emulator.
(If you already have a copy of lua51.dll from installing another lua script, this is fine, and you do not need to overwrite it.)

Setup
========
1) Open PunkRock.exe. A file manager window should appear. In it, select the .nds ROM you are playing. The program will read the file and extract the music, so it knows what to play.
2) In Desmume, while playing the game, run Soundproof.lua. Bizhawk is not currently supported.
3) Et voila.

Controls
========
You can toggle the music with Backspace. If you want to change which key is used, you can do so by editing the Soundproof.lua file: ctrl+f "backspace", then replace it with whichever key you like. A list of keys may be found [here](https://github.com/TASEmulators/desmume/blob/master/desmume/src/lua-engine.cpp#L4065).

Dependencies
========
https://github.com/kode54/SSEQPlayer: Used in PunkRock.exe to generate the samples for the music extracted from the .nds file. <br>
https://github.com/mackron/miniaudio: Used in PunkRock.exe to play the music, using the samples generated from the SSEQPlayer.

FAQ
========
- Q: Will this work with other .nds games?

    A: For DSPRE romhacks of Platinum, and vanilla Platinum itself: it should work out-the-box, although I haven't tested it extensively. Other NDS games will not immediately be compatible, however. 
    
- Q: Will you ever make it compatible with other .nds games? 

    A: Maybe other Pokemon games, but not right this minute.

- Q: Is it okay if I fork the project and do so myself?

    A: Be my guest! PunkRock.exe ought to be compatible with the majority of NDS games, and LiquidVoice.dll is just a "bridge" connecting the music player (PunkRock) with the lua script that detects game behavior (Soundproof). So the only thing that needs modifying will be Soundproof. How hard this is depends on what you're intending to modify:
    - For HGSS, DP, their romhacks, and binary hacks of Platinum: There are two specific static addresses that will need hunting down. You can find these by cloning the relevant decompilation project, generating a linker map file, and then searching it for sPlayer and sSoundSystem. Go ahead and copy the addresses you find into the SoundProof.lua file, and that should probably be sufficient. 
    - For (most) other NDS games: You'll want to find and replace those same static addresses, same as with Pokemon games. However, while the underlying NitroSystem and NitroSDK libraries should be consistent across most NDS games, how they're used is almost certainly not, so you'll most likely need to rewrite much of SoundProof.lua from scratch to accommodate this.
  
  If the game in question doesn't use NitroSystem or NitroSDK libraries, or doesn't use the SSEQ format for music, I'm afraid far more extensive reworks will be needed, and you'll basically have to start from scratch. 

- Q: What about 3DS games? <br>

    A: Unfortunately, almost none of this project would be reusable towards 3DS games, which have a different filesystem, a different sound architecture, and run on different emulators. I have no intention of supporting them at this time, and I would expect others to get little of value from this project in trying to do so themselves.

License
========
[MIT No Attribution](https://github.com/aws/mit-0)
