#pragma once

#include "Mod.hpp"

namespace kanan {
    // Cast On Target: using a skill (its hotkey or skill bar button) with a target loads it and heads
    // for the target while it loads; using it again, while loading or once loaded, keeps heading there
    // and uses it once it's ready and in range, just like clicking the target. Skills used on yourself
    // (Windmill and the like) go off in place on a second use once loaded, with no target needed.
    // Skills without a target (Defense, Counter, Healing...) just load as usual.
    //
    // The client already has everything after the decision: message 455h makes the character
    // controller attack the target with the loaded skill (CCharacterController::AttackTarget), the
    // same as a left click, and CCharacter::CommandProcessSkill uses a loaded skill in place.
    class SkillHotkey : public Mod {
    public:
        SkillHotkey();

        std::string getName() override { return "Cast On Target"; }

        void onUI() override;

        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_isReady;     // the skill use function and the message functions were found
    };
}
