#include <windows.h>
#include <winuser.h>
#include <commctrl.h>
#include <filesystem>
#include <shobjidl.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <array>
#include <mutex>
#include <thread>
#include <algorithm>

#define MINIAUDIO_IMPLEMENTATION
#include "deps/miniaudio/miniaudio.h"
#include "deps/SSEQPlayer/Player.h"
#include "deps/SSEQPlayer/SDAT.h"
#include "deps/SSEQPlayer/SSEQ.h"
#include "deps/ROM/ROM.h"

// We need two players so that you can pause one and keep the place while the other one plays. This matters for entering/exiting battles, primarily.

static std::array<Player,                2> player; // 0 = field, 1 = battle
static std::array<std::unique_ptr<SDAT>, 2> sdat;
static std::array<uint32_t,              2> track; 
static uint32_t context = 0; 

static std::vector<std::array<int16_t, 2>> samples; // Each sample is 2 16-bit values
static std::vector<uint8_t> sdat_bytes;
static std::vector<std::string> seq_names;
static PseudoFile pf;

static std::mutex player_mutex;

static HWND g_slider, g_label;
static float g_volume = 0.5f; // The volume we set through the slider in the window.

static std::array<uint32_t, 3> paused;
static float fade = 1.0f;
static uint32_t mute = 1;

// Basic GUI
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_CREATE:
            g_label = CreateWindowExW( // "What song is playing?"
                0,                     // dwExStyle
                L"STATIC",             // lpClassName
                L"Nothing playing",    // lpWindowName
                WS_CHILD | WS_VISIBLE, // dwStyle
                20,                    // X
                20,                    // Y
                300,                   // nWidth
                20,                    // nHeight
                hwnd,                  // hWndParent
                NULL,                  // hMenu
                NULL,                  // hInstance
                NULL                   // lpParam
            );

            g_slider = CreateWindowExW( // Volume slider
                0,                                // dwExStyle
                TRACKBAR_CLASSW,                  // lpClassName
                NULL,                             // lpWindowName
                WS_CHILD | WS_VISIBLE | TBS_HORZ, // dwStyle
                20,                               // X
                60,                               // Y
                300,                              // nWidth
                30,                               // nHeight
                hwnd,                             // hWndParent
                NULL,                             // hMenu
                NULL,                             // hInstance
                NULL                              // lpParam
            );
            {
                LPARAM icon = (LPARAM) LoadImageW(
                    GetModuleHandle(NULL), // hInst
                    L"IDI_PR_ICON",        // name
                    IMAGE_ICON,            // type
                    32,                    // cx
                    32,                    // cy
                    LR_SHARED              // fuLoad
                );
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, icon);
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, icon);
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL2, icon);
                SendMessageW(g_slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
                SendMessageW(g_slider, TBM_SETPOS, TRUE, 50);
            }
            return 0;

        case WM_HSCROLL:
            if ((HWND)lp == g_slider)
                g_volume = SendMessageW(g_slider, TBM_GETPOS, 0, 0) / 100.0f;
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Opens a file window asking the user to select the relevant .nds rom.
std::filesystem::path PickRom()
{
    std::filesystem::path result;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr))
    {
        return result;
    }

    IFileOpenDialog *dialog = NULL;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr))
    {
        COMDLG_FILTERSPEC types[] = {
            { L"NDS roms", L"*.nds" },
        };
        dialog->SetFileTypes(ARRAYSIZE(types), types);

        DWORD flags = 0;
        dialog->GetOptions(&flags);
        dialog->SetOptions(flags | FOS_FORCEFILESYSTEM);

        hr = dialog->Show(NULL);
        if (SUCCEEDED(hr))
        {
            IShellItem *item = NULL;
            hr = dialog->GetResult(&item);
            if (SUCCEEDED(hr))
            {
                PWSTR path = NULL;
                hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(hr))
                {
                    result = std::filesystem::path(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    CoUninitialize();
    return result;
}

void set_song_name(std::string name)
{
    std::wstring wide_name = std::wstring(name.begin(), name.end());
    SetWindowTextW(g_label, wide_name.c_str());
}

// Its a little annoying for me to fetch the SSEQ myself, so I went ahead and piggybacked off SSEQPlayer's built-in stuff. Unfortunately, that means remaking the SDAT object each time. 
int set_track(uint32_t sseq) // Caller must hold the mutex.
{
    try
    {
        pf.pos = 0;
        track[context] = sseq;

        player[context].Stop(true);
        sdat[context] = std::make_unique<SDAT>(pf, track[context]);
        player[context].Setup(sdat[context]->sseq.get());
        set_song_name(seq_names[track[context]]);  

        return 0;
    }
    catch (...) { return -1; }
}

void pipe_thread()
{
    // Pipe for communication with the .dll
    HANDLE pipe = CreateNamedPipeW(
        LR"(\\.\pipe\round)",                                  // lpName
        PIPE_ACCESS_INBOUND,                                   // dwOpenMode
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, // dwPipeMode
        1,                                                     // nMaxInstances
        0,                                                     // nOutBufferSize
        sizeof(uint32_t),                                      // nInBufferSize
        0,                                                     // nDefaultTimeOut
        NULL                                                   // lpSecurityAttributes (default security)
    );
    if (pipe == INVALID_HANDLE_VALUE)
    {
        std::cerr << "CreateNamedPipe failed: " << GetLastError() << "\n";
        return;
    }
    
    enum Command {
        COMMAND_SET_PAUSE,    // Legal values: 0 or 1
        COMMAND_SET_MUTE,     // Legal values: 0 or 1
        COMMAND_SET_FADE,     // Legal values: 0 to 128. Fixed-point, basically.
        COMMAND_SET_TRACK,    // Legal values: 0 to iirc 0x854. Some values in-between have no music.
        COMMAND_SET_PLAYER,   // Legal values: 0 (field) or 1 (battle). Switches without setting a track, so it'll resume any music that was playing.
        COMMAND_CLOSE_PLAYER, // Values do not matter. If this command is sent, close the program.
    };
    uint32_t data; // 1 byte for command | 1 byte for target (0 = field, 1 = bgm, 2 = global) | 2 bytes for value (for example, track num)
    DWORD read;
    while (true)
    {
        ConnectNamedPipe(pipe, NULL);
        while(ReadFile(pipe, &data, sizeof(data), &read, NULL) && read == sizeof(data))
        {
            Command command = static_cast<Command>(data >> 24);
            uint8_t target = data >> 16;
            uint16_t value = data; // Discards the command and target bytes.
            switch (command)
            {
                case COMMAND_SET_PAUSE:
                    if (target > 2)
                    {
                        std::cerr << "bad target " << (int) target << " for command " << command << "\n"; 
                        std::exit(-1);
                    }
                    paused[target] = value;
                    break;

                case COMMAND_SET_MUTE:
                    mute = value;
                    break;
                
                case COMMAND_SET_FADE:
                    fade = value / 128.0;
                    break;
                
                case COMMAND_SET_TRACK:
                    if (target > 1)
                    {
                        std::cerr << "bad target " << (int) target << " for command " << command << "\n"; 
                        std::exit(-1);
                    }
                    if(value < seq_names.size() && seq_names[value] != "")
                    {
                        std::lock_guard<std::mutex> lock(player_mutex);
                        context = target;
                        set_track(value);
                    }
                    break;

                case COMMAND_SET_PLAYER:
                if (target > 1)
                    {
                        std::cerr << "bad target " << (int) target << " for command " << command << "\n"; 
                        std::exit(-1);
                    }
                    {
                        std::lock_guard<std::mutex> lock(player_mutex);
                        context = target;
                        if(track[context] < seq_names.size() && seq_names[track[context]] != "")
                        {
                            set_song_name(seq_names[track[context]]);
                        }
                    }
                    break;
                
                case COMMAND_CLOSE_PLAYER:
                    std::exit(0);
                    break;
            }

        }
        DisconnectNamedPipe(pipe);
    }
}

// The function we give to miniaudio for its thread.
void audio_callback(ma_device *device, void *out, const void *in, ma_uint32 frames)
{
    if (paused[2] || paused[context])
    {
        std::memset(out, 0, frames * 4);
    }
    else
    {
        std::vector<uint8_t> raw_data(frames * 4);
        {
            std::lock_guard<std::mutex> lock(player_mutex);
            player[context].GenerateSamples(raw_data, 0, frames);
        }

        float volume = g_volume * mute * fade;
        samples.resize(frames);
        std::memcpy(samples.data(), raw_data.data(), frames * 4);
        for (int i = 0; i < frames; i++)
        {
            samples[i][0] *= volume * volume;
            samples[i][1] *= volume * volume;
        }
        std::memcpy(out, samples.data(), frames * 4);
    }
}

int main()
{
    // Display the GUI
    INITCOMMONCONTROLSEX icex; // First, we make sure Comctl32.dll is loaded.
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC  = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex); 

    WNDCLASSEXW wc = {}; // Next, we fill the window class info, using the function we made earlier.
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = L"PunkRock";
    wc.cbSize        = sizeof(wc);
    if(!RegisterClassExW(&wc))
    {
        std::cout << "RegisterClassExW failed: " << GetLastError() << "\n";
    }

    HWND hwnd = CreateWindowExW( // Next, we make the window itself.
        0,                                    // dwExStyle
        L"PunkRock",                          // lpClassName
        L"Punk Rock",                         // lpWindowName
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, // dwStyle
        CW_USEDEFAULT,                        // x
        CW_USEDEFAULT,                        // y
        350,                                  // nWidth
        140,                                  // nHeight
        NULL,                                 // hWndParent
        NULL,                                 // hMenu
        wc.hInstance,                         // hInstance
        NULL                                  // lpParam
    );
    if(!hwnd)
    {
        std::cout << "CreateWindowExW failed: " << GetLastError() << "\n";
    }

    ShowWindow(hwnd, SW_SHOW); // Finally, we show it.
    MSG msg;

    // Prompt the user for the ROM, and then fetch the SDAT from the ROM. 
    // The ROM class is one I've written before for other projects, adapted pretty easily for c++.
    const std::filesystem::path romPath = PickRom();
    ROM rom(romPath);
    auto [sdat_start, sdat_end] = rom.GetFileAddr("data/sound/pl_sound_data.sdat");
    sdat_bytes = rom.Read8Range(sdat_start, sdat_end);
    pf.data = &sdat_bytes;
    pf.pos = 0;
    seq_names = rom.GetSEQList(sdat_start);

    // Setup the device for playback and set the sample rate.
    ma_device device;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_s16;
    cfg.playback.channels = 2;
    cfg.sampleRate        = 0;
    cfg.dataCallback      = audio_callback;
    if (ma_device_init(NULL, &cfg, &device) != MA_SUCCESS)
    {
        return -1;
    }
    player[0].sampleRate = device.sampleRate;
    player[1].sampleRate = device.sampleRate;

    // Set the pipe reading to happen on its own. It'll keep going until the process terminates.
    std::thread piping(pipe_thread);
    piping.detach();

    // Audio playback + window controls.
    ma_device_start(&device);
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ma_device_uninit(&device);
    return 0;
}
