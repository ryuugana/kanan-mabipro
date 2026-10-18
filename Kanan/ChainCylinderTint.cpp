#include <Scan.hpp>

#include "Log.hpp"
#include "CharacterHook.hpp"
#include "ChainCylinderTint.hpp"

using namespace std;

namespace kanan {
    static ChainCylinderTint* g_chainCylinderTint{ nullptr };

    // G13 client layout (Pleione.dll), same render path as MeditationTint:
    //   pleione::CCharacter +0x198 -> render entry
    //   render entry vtable +0xB0 -> SetColor(raw)
    //   pleione::CCharacter +0x90 -> core::ISkillMgr*
    //
    // History check this replaces the Meditation check with (SkillID 35015,
    // 0x88C7 Chain Cylinder):
    //   client.exe: push 0x000088C7 -> call [vtable+0x60] skill lookup ->
    //   call skill-enabled predicate -> test al, al / je skip. Addresses move
    //   on reload, so this mod resolves the equivalent call at runtime.
    namespace offsets {
        constexpr uintptr_t renderEntry = 0x198;
        constexpr uintptr_t setColor = 0xB0;
        constexpr uintptr_t skillMgr = 0x90;
    }

    // Chain Cylinder skill from skilldb: SkillID 35015 (0x88C7).
    constexpr uint32_t chainCylinderSkill = 35015; // 0x88C7

    using SetColor = void(__thiscall*)(uintptr_t renderEntry, uint32_t rawColor);
    using IsTurnedOnFn = bool(__thiscall*)(void* skillMgr, uint16_t skillID);

    static IsTurnedOnFn g_isTurnedOn{ nullptr };

    struct CharacterState {
        uintptr_t renderEntry;
        uintptr_t vtable;
        uintptr_t skillMgr;
        bool chainCylinderOn;
    };

    // Kept free of C++ objects so the SEH guard can protect it.
    // History check, expressed safely: SkillID 35015 (Chain Cylinder) is
    // queried on the character's own core::ISkillMgr.
    // IsTurnedOn checks the skill's toggle flag (offset +0xE, bit 2), which
    // correctly reports when a charge skill like Chain Cylinder is active,
    // unlike GetProgress which returns charge progress (0 = idle, etc.).
    static bool isChainCylinderEnabled(uintptr_t skillMgr) {
        if (skillMgr == 0 || g_isTurnedOn == nullptr) {
            return false;
        }

        __try {
            return g_isTurnedOn((void*)skillMgr, chainCylinderSkill) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Kept free of C++ objects so the SEH guard can protect it.
    static bool readCharacterState(uintptr_t character, CharacterState& out) {
        __try {
            out.renderEntry = *(uintptr_t*)(character + offsets::renderEntry);

            if (out.renderEntry == 0) {
                return false;
            }

            out.vtable = *(uintptr_t*)out.renderEntry;
            out.skillMgr = *(uintptr_t*)(character + offsets::skillMgr);
            out.chainCylinderOn = isChainCylinderEnabled(out.skillMgr);

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    ChainCylinderTint::ChainCylinderTint()
        : m_isEnabled{ false },
        m_color{ 1.0f, 0.55f, 0.15f, 1.0f },
        m_strength{ 1.0f },
        m_isReady{ false }
    {
        g_chainCylinderTint = this;

        if (auto standard = GetModuleHandleA("Standard.dll")) {
            // Resolve IsTurnedOn from Standard.dll (core::ISkillMgr::IsTurnedOn)
            // at runtime so the mod works across reloads.
            // Symbol Standard.core::ISkillMgr::IsTurnedOn resolves to 0x100ED61C;
            // Standard.dll base is 0x10000000, so the offset is 0xED61C.
            auto isTurnedOn = (uint8_t*)standard + 0xED61C;
            if (isTurnedOn) {
                typedef bool(__thiscall* IsTurnedOnFn_t)(void*, uint16_t);
                g_isTurnedOn = (IsTurnedOnFn_t)isTurnedOn;
            }
        }

        // Hook character update to check skill state on every update
        if (addCharacterUpdateCallback(std::bind(&ChainCylinderTint::onCharacterUpdate, this, std::placeholders::_1))) {
            m_isReady = true;
        } else {
            log("[ChainCylinderTint] Failed to hook character update");
        }
    }

    ChainCylinderTint::~ChainCylinderTint() {
        scoped_lock<mutex> _{ m_mutex };
        g_chainCylinderTint = nullptr;
    }

    void ChainCylinderTint::onUI() {
        if (!m_isReady) {
            return;
        }

        ImGui::TextWrapped("Tints characters that have Chain Cylinder (35015) enabled with a custom color.");
        ImGui::Spacing();
        ImGui::Checkbox("Enabled##ChainCylinderTint", &m_isEnabled);
        ImGui::ColorEdit3("Color##ChainCylinderTint", &m_color.x);
        ImGui::SliderFloat("Strength##ChainCylinderTint", &m_strength, 0.0f, 1.0f, "%.2f");
    }

    void ChainCylinderTint::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("ChainCylinderTint.Enabled").value_or(false);
        m_color = ImGui::ColorConvertU32ToFloat4(cfg.get<unsigned int>("ChainCylinderTint.Color").value_or(IM_COL32(255, 140, 38, 255)));
        m_strength = cfg.get<float>("ChainCylinderTint.Strength").value_or(1.0f);
    }

    void ChainCylinderTint::onConfigSave(Config& cfg) {
        cfg.set<bool>("ChainCylinderTint.Enabled", m_isEnabled);
        cfg.set<unsigned int>("ChainCylinderTint.Color", ImGui::ColorConvertFloat4ToU32(m_color));
        cfg.set<float>("ChainCylinderTint.Strength", m_strength);
    }

    uint32_t ChainCylinderTint::tintRawColor() const {
        auto channel = [this](float value) {
            value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            return (uint32_t)(255.0f - (255.0f - value * 255.0f) * m_strength + 0.5f);
        };

        return channel(m_color.x) << 16 | channel(m_color.y) << 8 | channel(m_color.z);
    }

    bool ChainCylinderTint::hookVtable(uintptr_t vtable) {
        if (m_originalSetColor.count(vtable) != 0) {
            return true;
        }

        auto slot = (uintptr_t*)(vtable + offsets::setColor);
        DWORD oldProtect{};

        if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            log("[ChainCylinderTint] Failed to unprotect render entry vtable %p", vtable);
            return false;
        }

        m_originalSetColor[vtable] = *slot;
        *slot = (uintptr_t)&ChainCylinderTint::hookedSetColor;
        VirtualProtect(slot, sizeof(uintptr_t), oldProtect, &oldProtect);

        log("[ChainCylinderTint] Hooked render entry vtable %p (SetColor %p)", vtable, m_originalSetColor[vtable]);

        return true;
    }

    void ChainCylinderTint::setColor(uintptr_t renderEntry, uint32_t rawColor) {
        __try {
            auto original = m_originalSetColor.find(*(uintptr_t*)renderEntry);

            if (original != m_originalSetColor.end()) {
                ((SetColor)original->second)(renderEntry, rawColor);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // Swallow during shutdown - renderEntry may be dangling.
        }
    }

    void ChainCylinderTint::onCharacterUpdate(uintptr_t character) {
        if (!m_isEnabled) {
            scoped_lock<mutex> _{ m_mutex };

            if (m_tintedCount == 0) {
                return;
            }
        }

        CharacterState state{};

        if (!readCharacterState(character, state)) {
            return;
        }

        scoped_lock<mutex> _{ m_mutex };

        if (!hookVtable(state.vtable)) {
            return;
        }

        auto& color = m_characters[state.renderEntry];
        auto shouldTint = m_isEnabled && state.chainCylinderOn;
        auto tint = tintRawColor();

        if (shouldTint && (!color.isTinted || color.tintColor != tint)) {
            setColor(state.renderEntry, tint);
            m_tintedCount += color.isTinted ? 0 : 1;
            color.isTinted = true;
            color.tintColor = tint;
        }
        else if (!shouldTint && color.isTinted) {
            setColor(state.renderEntry, color.gameColor);
            color.isTinted = false;
            --m_tintedCount;
        }
    }

    void __fastcall ChainCylinderTint::hookedSetColor(uintptr_t renderEntry, uintptr_t edx, uint32_t rawColor) {
        auto self = g_chainCylinderTint;

        if (self == nullptr) {
            return;
        }

        scoped_lock<mutex> _{ self->m_mutex };

        auto& color = self->m_characters[renderEntry];

        color.gameColor = rawColor;

        self->setColor(renderEntry, color.isTinted ? color.tintColor : rawColor);
    }
}
