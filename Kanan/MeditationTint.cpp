#include "Log.hpp"
#include "CharacterHook.hpp"
#include "MeditationTint.hpp"

using namespace std;

namespace kanan {
    static MeditationTint* g_meditationTint{ nullptr };

    // G13 client layout (Pleione.dll):
    //   pleione::CCharacter +0x198 -> render entry
    //   render entry vtable +0xB0 -> SetColor(raw): what the client calls with a character's
    //     condition color; forwards to the character's CJointAttacherContext, which tints every
    //     attached mesh. A raw value with a zero top nibble is a static 0x00RRGGBB color that
    //     multiplies the textures (0xFFFFFF / 0 = untinted); a non-zero top nibble selects an
    //     animated palette function (flashy colors).
    //   CCharacter vtable +0x80 -> core::IStateMgr
    namespace offsets {
        constexpr uintptr_t renderEntry = 0x198;
        constexpr uintptr_t setColor = 0xB0;
        constexpr uintptr_t getStateMgr = 0x80;
    }

    using GetPointer = uintptr_t(__thiscall*)(uintptr_t object);
    using GetBool = bool(__thiscall*)(uintptr_t object);
    using SetColor = void(__thiscall*)(uintptr_t renderEntry, uint32_t rawColor);

    static GetBool g_isMeditating{ nullptr };

    struct CharacterState {
        uintptr_t renderEntry;
        uintptr_t vtable;
        bool isMeditating;
    };

    // Kept free of C++ objects so the SEH guard can protect it.
    static bool readCharacterState(uintptr_t character, CharacterState& out) {
        __try {
            out.renderEntry = *(uintptr_t*)(character + offsets::renderEntry);

            if (out.renderEntry == 0) {
                return false;
            }

            out.vtable = *(uintptr_t*)out.renderEntry;

            auto getStateMgr = (GetPointer)(*(uintptr_t**)character)[offsets::getStateMgr / sizeof(uintptr_t)];
            auto stateMgr = getStateMgr(character);

            out.isMeditating = stateMgr != 0 && g_isMeditating(stateMgr);

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    MeditationTint::MeditationTint()
        : m_isEnabled{ false },
        m_color{ 0.7f, 0.4f, 1.0f, 1.0f },
        m_strength{ 1.0f },
        m_isReady{ false },
        m_mutex{},
        m_characters{},
        m_originalSetColor{}
    {
        log("[MeditationTint] Entering constructor...");

        g_meditationTint = this;

        auto standard = GetModuleHandleA("Standard.dll");

        if (standard != nullptr) {
            g_isMeditating = (GetBool)GetProcAddress(standard, "?IsMeditating@IStateMgr@core@@QAE_NXZ");
        }

        if (g_isMeditating == nullptr) {
            log("[MeditationTint] Failed to find IStateMgr::IsMeditating");
            log("[MeditationTint] Leaving constructor");
            return;
        }

        m_isReady = addCharacterUpdateCallback([this](uintptr_t character) {
            onCharacterUpdate(character);
        });

        log("[MeditationTint] %s", m_isReady ? "Ready" : "Failed to hook character update");
        log("[MeditationTint] Leaving constructor");
    }

    MeditationTint::~MeditationTint() {
        scoped_lock<mutex> _{ m_mutex };

        for (auto& [vtable, original] : m_originalSetColor) {
            auto slot = (uintptr_t*)(vtable + offsets::setColor);
            DWORD oldProtect{};

            if (VirtualProtect(slot, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                *slot = original;
                VirtualProtect(slot, sizeof(uintptr_t), oldProtect, &oldProtect);
            }
        }

        g_meditationTint = nullptr;
    }

    void MeditationTint::onUI() {
        if (!m_isReady) {
            return;
        }

        if (ImGui::TreeNode("Meditation Tint")) {
            ImGui::TextWrapped("Tints characters that are meditating with a custom color.");
            ImGui::Spacing();
            ImGui::Checkbox("Enabled##MeditationTint", &m_isEnabled);
            ImGui::ColorEdit3("Color##MeditationTint", &m_color.x);
            ImGui::SliderFloat("Strength##MeditationTint", &m_strength, 0.0f, 1.0f, "%.2f");
            ImGui::TreePop();
        }
    }

    void MeditationTint::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("MeditationTint.Enabled").value_or(false);
        m_color = ImGui::ColorConvertU32ToFloat4(cfg.get<unsigned int>("MeditationTint.Color").value_or(IM_COL32(178, 102, 255, 255)));
        m_strength = cfg.get<float>("MeditationTint.Strength").value_or(1.0f);
    }

    void MeditationTint::onConfigSave(Config& cfg) {
        cfg.set<bool>("MeditationTint.Enabled", m_isEnabled);
        cfg.set<unsigned int>("MeditationTint.Color", ImGui::ColorConvertFloat4ToU32(m_color));
        cfg.set<float>("MeditationTint.Strength", m_strength);
    }

    // Static 0x00RRGGBB. The color multiplies the textures, so strength blends it toward white.
    uint32_t MeditationTint::tintRawColor() const {
        auto channel = [this](float value) {
            value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);

            return (uint32_t)(255.0f - (255.0f - value * 255.0f) * m_strength + 0.5f);
        };

        return channel(m_color.x) << 16 | channel(m_color.y) << 8 | channel(m_color.z);
    }

    bool MeditationTint::hookVtable(uintptr_t vtable) {
        if (m_originalSetColor.count(vtable) != 0) {
            return true;
        }

        auto slot = (uintptr_t*)(vtable + offsets::setColor);
        DWORD oldProtect{};

        if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            log("[MeditationTint] Failed to unprotect render entry vtable %p", vtable);
            return false;
        }

        m_originalSetColor[vtable] = *slot;
        *slot = (uintptr_t)&MeditationTint::hookedSetColor;
        VirtualProtect(slot, sizeof(uintptr_t), oldProtect, &oldProtect);

        log("[MeditationTint] Hooked render entry vtable %p (SetColor %p)", vtable, m_originalSetColor[vtable]);

        return true;
    }

    // Calls the client's own setter, bypassing our hook.
    void MeditationTint::setColor(uintptr_t renderEntry, uint32_t rawColor) {
        auto original = m_originalSetColor.find(*(uintptr_t*)renderEntry);

        if (original != m_originalSetColor.end()) {
            ((SetColor)original->second)(renderEntry, rawColor);
        }
    }

    void MeditationTint::onCharacterUpdate(uintptr_t character) {
        CharacterState state{};

        if (!readCharacterState(character, state)) {
            return;
        }

        scoped_lock<mutex> _{ m_mutex };

        if (!hookVtable(state.vtable)) {
            return;
        }

        auto& color = m_characters[state.renderEntry];
        auto shouldTint = m_isEnabled && state.isMeditating;
        auto tint = tintRawColor();

        if (shouldTint && (!color.isTinted || color.tintColor != tint)) {
            setColor(state.renderEntry, tint);
            color.isTinted = true;
            color.tintColor = tint;
        }
        else if (!shouldTint && color.isTinted) {
            setColor(state.renderEntry, color.gameColor);
            color.isTinted = false;
        }
    }

    // The client sets a character's color whenever its conditions change. Remember what it asked
    // for, but keep our tint on while the character is tinted.
    void MeditationTint::hookedSetColor(uintptr_t renderEntry, uintptr_t edx, uint32_t rawColor) {
        auto self = g_meditationTint;

        scoped_lock<mutex> _{ self->m_mutex };

        auto& color = self->m_characters[renderEntry];

        color.gameColor = rawColor;

        self->setColor(renderEntry, color.isTinted ? color.tintColor : rawColor);
    }
}
