-- Test helper: logs to UE4SS.log whenever the engine reports one of these FKeys
-- as pressed/released on the local PlayerController. Install next to
-- KeyboardLayoutFix, press keys in game, then grep UE4SS.log for [KeyTest].
local UEHelpers = require("UEHelpers")

local KEYS = {
    "One", "Two", "Three", "Zero", "Hyphen", "Equals", "Tilde", "Semicolon", "LeftBracket",
    "Y", "Z", "W",
    -- what the Czech number row resolves to without the fix
    "E_AccentAigu", "A_AccentGrave", "E_AccentGrave", "Section",
}

local down = {}

LoopAsync(30, function()
    ExecuteInGameThread(function()
        local pc = UEHelpers.GetPlayerController()
        if not pc or not pc:IsValid() then return end
        for _, name in ipairs(KEYS) do
            local is_down = pc:IsInputKeyDown({ KeyName = FName(name) })
            if is_down ~= (down[name] or false) then
                down[name] = is_down
                print(string.format("[KeyTest] %s %s\n", name, is_down and "DOWN" or "up"))
            end
        end
    end)
    return false
end)

print("[KeyTest] polling started\n")
