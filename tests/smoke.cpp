// Loads dist/KeyboardLayoutFix/dlls/main.dll the way UE4SS does, then checks what
// MapVirtualKey(VK_TO_CHAR) returns for physical keys under the Czech layout.
// Usage: smoke.exe <path to main.dll>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>
#include <initializer_list>
#include <iterator>

struct Case
{
    UINT scan;
    wchar_t expected; // physical mode
    const char* key;
};

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
    {
        fwprintf(stderr, L"usage: smoke <main.dll>\n");
        return 2;
    }

    // Czech QWERTZ for this thread only.
    if (!ActivateKeyboardLayout(LoadKeyboardLayoutW(L"00000405", KLF_NOTELLSHELL), 0))
    {
        fwprintf(stderr, L"could not activate the Czech layout\n");
        return 2;
    }

    const Case cases[] = {
        {0x02, L'1', "1"}, {0x03, L'2', "2"}, {0x0B, L'0', "0"}, {0x0C, L'-', "-"}, {0x0D, L'=', "="},
        {0x15, L'Y', "Y (CZ Z)"}, {0x2C, L'Z', "Z (CZ Y)"}, {0x11, L'W', "W"}, {0x1A, L'[', "["}, {0x27, L';', ";"},
        {0x29, L'`', "`"}, {0x35, L'/', "/"}, {0x56, L'\\', "ISO <>"},
    };

    auto char_for_scan = [](UINT scan, bool ex) {
        const UINT vk = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK);
        return ex ? MapVirtualKeyExW(vk, MAPVK_VK_TO_CHAR, GetKeyboardLayout(0)) : MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR);
    };

    UINT before[std::size(cases)];
    for (size_t i = 0; i < std::size(cases); ++i)
    {
        before[i] = char_for_scan(cases[i].scan, false);
    }
    const UINT numpad_before = MapVirtualKeyW(VK_NUMPAD2, MAPVK_VK_TO_CHAR);
    const UINT divide_before = MapVirtualKeyW(VK_DIVIDE, MAPVK_VK_TO_CHAR);

    HMODULE mod = LoadLibraryW(argv[1]);
    auto start_mod = mod ? reinterpret_cast<void* (*)()>(GetProcAddress(mod, "start_mod")) : nullptr;
    if (!start_mod || !GetProcAddress(mod, "uninstall_mod"))
    {
        fwprintf(stderr, L"failed to load mod or find exports\n");
        return 2;
    }
    if (start_mod() != nullptr)
    {
        fwprintf(stderr, L"start_mod should return nullptr\n");
        return 1;
    }
    FreeLibrary(mod); // UE4SS does this on shutdown; the mod pins itself, so hooks must survive.

    int failures = 0;
    for (size_t i = 0; i < std::size(cases); ++i)
    {
        for (bool ex : {false, true})
        {
            const UINT after = char_for_scan(cases[i].scan, ex);
            const bool ok = after == cases[i].expected;
            failures += !ok;
            printf("%-4s %-10s %-3s before U+%04X  after U+%04X  %s\n", ok ? "ok" : "FAIL", cases[i].key, ex ? "Ex" : "", before[i] & 0xFFFF,
                   after, ok ? "" : "<-- unexpected");
        }
    }

    const bool numpad_ok = MapVirtualKeyW(VK_NUMPAD2, MAPVK_VK_TO_CHAR) == numpad_before;
    const bool divide_ok = MapVirtualKeyW(VK_DIVIDE, MAPVK_VK_TO_CHAR) == divide_before;
    const bool vsc_ok = MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC) == 0x39;
    printf("%-4s numpad/divide/VK_TO_VSC untouched\n", numpad_ok && divide_ok && vsc_ok ? "ok" : "FAIL");
    failures += !(numpad_ok && divide_ok && vsc_ok);

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
