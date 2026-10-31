# KeyboardLayoutFix

A UE4SS C++ mod that makes Unreal Engine games read the keyboard as if a **US layout** were active, so you don't have to switch Windows to English before playing on QWERTZ (Czech, German, …), AZERTY or other layouts.

Without it, on a Czech keyboard the number row types `+ ě š č ř ž ý á í é` and Unreal games either ignore those keys or treat `0` as `É`. With it, the number row, punctuation and console key all work, and typing in chat or text boxes still gives your normal characters.

## Install

1. Install [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS/releases) in the game (any 3.x version should work; the mod doesn't depend on the UE4SS SDK).
2. Copy the `KeyboardLayoutFix` folder into the game's UE4SS `Mods` folder:
   ```
   <Game>/<Project>/Binaries/Win64/ue4ss/Mods/KeyboardLayoutFix/
       dlls/main.dll
       config.ini
       enabled.txt
   ```
   (Older UE4SS installs use `Binaries/Win64/Mods/` instead.)
3. Start the game.

**Don't use it in games with anti-cheat** (Easy Anti-Cheat, BattlEye). Injecting DLLs there can get you banned.

## Config (`config.ini`)

| Setting | Values | |
|---|---|---|
| `Mode` | `physical` (default) | Every key acts by its position on a US keyboard. On QWERTZ, the key labeled Z acts as Y. Default bindings land where the developers placed them. |
| | `label` | Letters keep what's printed on them. Only the number row and punctuation are fixed. |
| `Debug` | `0` / `1` | Log every key translation to `KeyboardLayoutFix.log` in the mod folder. |

## How it works

Unreal's Windows input does roughly this on every key press:

```cpp
CharCode = MapVirtualKey(VirtualKey, MAPVK_VK_TO_CHAR);   // depends on your layout
Key = KeyMapVirtualToEnum[VirtualKey] ?? KeyMapCharToEnum[CharCode];
```

At startup, the engine also builds its punctuation key table the same way. The mod hooks `user32!MapVirtualKeyW` and `MapVirtualKeyExW` with MinHook and answers `MAPVK_VK_TO_CHAR` for the main key block from a fixed US scan-code table. Numpad, navigation and function keys are left alone. Text input arrives through `WM_CHAR`, which this doesn't touch.

The hook is a Windows API hook, not an engine signature, so the same DLL works across UE4 and UE5 versions. `start_mod()` installs the hooks and returns `nullptr`, which UE4SS treats as "no mod object" and never calls into. So the DLL doesn't link against the UE4SS SDK and isn't tied to its ABI. The DLL pins itself in memory so UE4SS unloading it can't leave dangling hooks.

## Build

Requires Visual Studio 2022 (C++ workload) and CMake 3.20+. MinHook is fetched automatically.

```bash
cmake -B build -A x64
cmake --build build --config Release
```

The ready-to-copy mod folder ends up in `dist/KeyboardLayoutFix/`. To copy straight into a game after each build:

```bash
cmake -B build -A x64 -DKLF_INSTALL_TO="C:/…/Binaries/Win64/ue4ss/Mods"
```

That copies `main.dll` and `enabled.txt` but never overwrites an existing `config.ini`.

## Tests

- `tests/smoke.cpp` loads the DLL the way UE4SS does, with the Czech layout active, and checks every translation. Run it with `ctest -C Release --test-dir build`.
- `tests/KeyTestMod/` is a UE4SS Lua mod that logs which `FKey`s the engine reports as held (`[KeyTest] Two DOWN` in `UE4SS.log`). Use it to verify a specific game.

Tested on Marbles on Stream (UE 4.27, UE4SS 3.0.1) with a Czech keyboard. Without the fix, the engine saw `0` as `E_AccentAigu`, the key left of `1` as `Semicolon`, and `1`, `2`, `=`, `[`, `;` not at all. With it, all of them came through as `One`, `Two`, `Zero`, `Hyphen`, `Equals`, `Tilde`, `LeftBracket`, `Semicolon`, `Y` and `Z`.

## Limitations

- **Late loading:** If a game's UE4SS loads C++ mods after the engine has built its key table, punctuation keys may stay unfixed. The number row and letters are translated on every press, so they still work.
- **Custom input:** Games that read the keyboard with their own raw input or SDL code instead of Unreal's input aren't affected. They usually don't have this bug anyway.
