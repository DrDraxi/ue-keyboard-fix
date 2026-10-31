// KeyboardLayoutFix - UE4SS C++ mod that makes Unreal Engine games read keys
// as if a US keyboard layout were active, without switching the Windows layout.
//
// Unreal's Windows input path does roughly:
//     CharCode = MapVirtualKey(VirtualKey, MAPVK_VK_TO_CHAR);   // layout dependent
//     Key = KeyMapVirtualToEnum[VirtualKey] ?? KeyMapCharToEnum[CharCode];
// On layouts like Czech QWERTZ the number row maps to e.g. 'ě', which has no FKey,
// so the press is dropped. We hook MapVirtualKeyW/ExW and answer MAPVK_VK_TO_CHAR
// from a fixed US table instead. Text input (WM_CHAR) is untouched.
//
// The mod intentionally does not link against the UE4SS SDK: start_mod() installs
// the hooks and returns nullptr, which UE4SS treats as "no user mod object" and
// never dereferences. That keeps the DLL independent of the UE4SS version/ABI.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <MinHook.h>

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

namespace
{
    enum class Mode
    {
        Physical, // every key acts by position, like on a US keyboard (QWERTZ Y/Z swap)
        Label,    // letters keep their printed label, only digits/punctuation are fixed
    };

    using MapVirtualKeyW_t = UINT(WINAPI*)(UINT, UINT);
    using MapVirtualKeyExW_t = UINT(WINAPI*)(UINT, UINT, HKL);

    MapVirtualKeyW_t g_orig_map_vk = nullptr;
    MapVirtualKeyExW_t g_orig_map_vk_ex = nullptr;

    Mode g_mode = Mode::Physical;
    bool g_debug = false;
    bool g_started = false;
    std::filesystem::path g_mod_dir;
    std::mutex g_log_mutex;

    // Unshifted US character for each set-1 scan code of the main key block.
    wchar_t us_char_for_scancode(UINT scan)
    {
        static const wchar_t kTable[0x57] = {
            /* 0x00 */ 0,    0,    L'1', L'2', L'3', L'4', L'5', L'6', L'7', L'8', L'9', L'0', L'-', L'=', 0,    0,
            /* 0x10 */ L'Q', L'W', L'E', L'R', L'T', L'Y', L'U', L'I', L'O', L'P', L'[', L']', 0,    0,    L'A', L'S',
            /* 0x20 */ L'D', L'F', L'G', L'H', L'J', L'K', L'L', L';', L'\'', L'`', 0,   L'\\', L'Z', L'X', L'C', L'V',
            /* 0x30 */ L'B', L'N', L'M', L',', L'.', L'/', 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            /* 0x50 */ 0,    0,    0,    0,    0,    0,    L'\\', // 0x56: ISO key next to left Shift
        };
        return scan < std::size(kTable) ? kTable[scan] : 0;
    }

    // Only keys of the printable main block are remapped; numpad, navigation and
    // function keys keep their normal behaviour (some share scan codes with them).
    bool is_printable_vk(UINT vk)
    {
        return (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') || (vk >= VK_OEM_1 && vk <= VK_OEM_3) ||
               (vk >= VK_OEM_4 && vk <= VK_OEM_8) || vk == VK_OEM_102;
    }

    void log(const char* fmt, ...)
    {
        std::lock_guard lock(g_log_mutex);
        FILE* file = nullptr;
        if (_wfopen_s(&file, (g_mod_dir / L"KeyboardLayoutFix.log").c_str(), L"a") != 0 || !file)
        {
            return;
        }
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(file, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list args;
        va_start(args, fmt);
        vfprintf(file, fmt, args);
        va_end(args);
        fputc('\n', file);
        fclose(file);
    }

    // Returns 0 when the key should be left to Windows.
    UINT translate_vk_to_char(UINT vk)
    {
        if (!is_printable_vk(vk))
        {
            return 0;
        }
        if (g_mode == Mode::Label && ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')))
        {
            return vk;
        }
        // The layout-specific VK -> scan code step recovers the physical key.
        const UINT scan = g_orig_map_vk(vk, MAPVK_VK_TO_VSC);
        return us_char_for_scancode(scan);
    }

    UINT handle_vk_to_char(UINT vk, UINT original)
    {
        const UINT translated = translate_vk_to_char(vk);
        if (g_debug)
        {
            log("VK 0x%02X: layout char 0x%04X -> %s 0x%04X", vk, original, translated ? "US" : "kept", translated ? translated : original);
        }
        return translated ? translated : original;
    }

    // user32 may implement MapVirtualKeyW on top of MapVirtualKeyExW; the guard keeps
    // the inner call (and our own VK -> scan code lookup) on the original path.
    thread_local bool t_in_hook = false;

    UINT WINAPI hooked_map_vk(UINT code, UINT map_type)
    {
        if (map_type != MAPVK_VK_TO_CHAR || t_in_hook)
        {
            return g_orig_map_vk(code, map_type);
        }
        t_in_hook = true;
        const UINT result = handle_vk_to_char(code, g_orig_map_vk(code, map_type));
        t_in_hook = false;
        return result;
    }

    UINT WINAPI hooked_map_vk_ex(UINT code, UINT map_type, HKL layout)
    {
        if (map_type != MAPVK_VK_TO_CHAR || t_in_hook)
        {
            return g_orig_map_vk_ex(code, map_type, layout);
        }
        t_in_hook = true;
        const UINT result = handle_vk_to_char(code, g_orig_map_vk_ex(code, map_type, layout));
        t_in_hook = false;
        return result;
    }

    void load_config()
    {
        const auto ini = (g_mod_dir / L"config.ini").wstring();
        wchar_t mode[32]{};
        GetPrivateProfileStringW(L"KeyboardLayoutFix", L"Mode", L"physical", mode, static_cast<DWORD>(std::size(mode)), ini.c_str());
        g_mode = _wcsicmp(mode, L"label") == 0 ? Mode::Label : Mode::Physical;
        g_debug = GetPrivateProfileIntW(L"KeyboardLayoutFix", L"Debug", 0, ini.c_str()) != 0;
    }

    bool create_hook(const char* name, LPVOID detour, LPVOID* original)
    {
        const MH_STATUS status = MH_CreateHookApi(L"user32", name, detour, original);
        if (status != MH_OK)
        {
            log("Failed to hook %s: %s", name, MH_StatusToString(status));
            return false;
        }
        return true;
    }

    void start()
    {
        if (g_started)
        {
            return; // UE4SS may construct the mod again on reload; hooks stay installed.
        }
        g_started = true;

        HMODULE self = nullptr;
        // Pin the DLL: UE4SS FreeLibrary()s mods on shutdown/reload, which would leave
        // the hooks jumping into unmapped code.
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&start), &self);
        wchar_t dll_path[MAX_PATH]{};
        GetModuleFileNameW(self, dll_path, MAX_PATH);
        g_mod_dir = std::filesystem::path(dll_path).parent_path().parent_path(); // <mod>/dlls/main.dll -> <mod>

        load_config();
        log("KeyboardLayoutFix starting, mode=%s, debug=%d, layout=0x%08llX", g_mode == Mode::Physical ? "physical" : "label",
            g_debug, reinterpret_cast<unsigned long long>(GetKeyboardLayout(0)));

        if (MH_Initialize() != MH_OK)
        {
            log("MH_Initialize failed");
            return;
        }
        const bool ok = create_hook("MapVirtualKeyW", reinterpret_cast<LPVOID>(&hooked_map_vk), reinterpret_cast<LPVOID*>(&g_orig_map_vk)) &&
                        create_hook("MapVirtualKeyExW", reinterpret_cast<LPVOID>(&hooked_map_vk_ex), reinterpret_cast<LPVOID*>(&g_orig_map_vk_ex));
        if (!ok)
        {
            return;
        }
        const MH_STATUS status = MH_EnableHook(MH_ALL_HOOKS);
        log(status == MH_OK ? "Hooks installed" : "MH_EnableHook failed: %s", MH_StatusToString(status));
    }
} // namespace

// UE4SS C++ mod entry points. The real signatures use RC::CppUserModBase*; any
// pointer type is ABI-identical, and returning nullptr opts out of all callbacks.
extern "C" __declspec(dllexport) void* start_mod()
{
    start();
    return nullptr;
}

extern "C" __declspec(dllexport) void uninstall_mod(void*)
{
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
