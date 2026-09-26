#include <memory>
#include <vector>

#include <FunctionHook.hpp>
#include <Scan.hpp>

#include "Log.hpp"
#include "CharacterHook.hpp"

using namespace std;

namespace kanan {
    static unique_ptr<FunctionHook> g_updateHook{};
    static vector<CharacterUpdateCallback> g_callbacks{};
    static bool g_triedHook{ false };

    static void __fastcall hookedUpdate(uintptr_t character, uintptr_t edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
        auto orig = (decltype(hookedUpdate)*)g_updateHook->getOriginal();

        orig(character, edx, a1, a2, a3, a4);

        for (auto& callback : g_callbacks) {
            callback(character);
        }
    }

    bool addCharacterUpdateCallback(CharacterUpdateCallback callback) {
        if (!g_triedHook) {
            g_triedHook = true;

            auto update = scan("Pleione.dll", "55 8B EC 83 EC 10 56 8B F1 8B 8E 98 01 00 00 85 C9 74 11 FF 75 14 FF 75 10 FF 75 0C FF 75 08");

            if (update) {
                log("[CharacterHook] Found character update %p", *update);

                g_updateHook = make_unique<FunctionHook>(*update, (uintptr_t)&hookedUpdate);

                if (g_updateHook->isValid()) {
                    log("[CharacterHook] Hooked character update");
                }
                else {
                    log("[CharacterHook] Failed to hook character update");
                }
            }
            else {
                log("[CharacterHook] Failed to find character update");
            }
        }

        if (g_updateHook == nullptr || !g_updateHook->isValid()) {
            return false;
        }

        g_callbacks.emplace_back(move(callback));

        return true;
    }
}
