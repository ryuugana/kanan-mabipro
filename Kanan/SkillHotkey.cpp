#include <memory>

#include <imgui.h>

#include <FunctionHook.hpp>
#include <Scan.hpp>

#include "CharacterHook.hpp"
#include "Log.hpp"
#include "SkillHotkey.hpp"

using namespace std;

namespace kanan {
    // Use the loaded skill on an entity: CMessage(455h, my character id) + u64 entity id, sent to the
    // client's own object 1000000000000001h. The character controller walks into the skill's range,
    // then uses it, as on a left click.
    constexpr uint32_t OP_ATTACK_TARGET = 0x455;
    constexpr uint64_t CLIENT_OBJECT = 0x1000000000000001ull;

    // core::ESkillProgress: the skill is loading, or loaded and waiting to be used.
    constexpr uint32_t PROGRESS_PREPARING = 1;
    constexpr uint32_t PROGRESS_WAITING = 2;

    // The skills this applies to, and what they're used on: 1 your current target, 2 yourself.
    // Unlisted skills just load as usual.
    struct SkillTarget {
        uint16_t skill;
        uint8_t type;
    };

    static const SkillTarget g_skillTargets[] = {
        { 20002, 1 },   // Smash
        { 20011, 1 },   // Charge
        { 20017, 1 },   // Lance Charge
        { 20018, 1 },   // Rage Impact
        { 20019, 1 },   // Bash
        { 21001, 1 },   // Ranged Attack
        { 21002, 1 },   // Magnum Shot
        { 21004, 1 },   // Arrow Revolver
        { 21006, 1 },   // Support Shot
        { 21007, 1 },   // Mirage Missile
        { 21009, 1 },   // Ego Bow Incarnate
        { 21010, 1 },   // Throw Attack
        { 21012, 1 },   // Spider Shot
        { 21014, 1 },   // Urgent Shot
        { 22001, 2 },   // Windmill
        { 22004, 1 },   // Final Hit
        { 22005, 1 },   // Ego Sword Incarnate
        { 22006, 1 },   // Ego Blunt Incarnate
        { 22011, 1 },   // Crash Shot
        { 23106, 1 },   // Gold Strike
        { 26000, 1 },   // Shuriken Mastery
        { 26001, 1 },   // Shuriken Charging
        { 26002, 1 },   // Kunai Rush / Shuriken Storm
        { 26003, 2 },   // Shadow Bind
        { 26007, 1 },   // Abyss Recall / Cherry Blossom Wind
        { 27002, 1 },   // Dorcha Snatch
        { 27003, 1 },   // Chain Impale
        { 27004, 2 },   // Raging Thrust
        { 27005, 1 },   // Spinning Slash
        { 27006, 1 },   // Chain Crush
        { 27007, 1 },   // Chain Sweeping
        { 27010, 1 },   // Death Marker
        { 27012, 1 },   // Tuairim Explosion
        { 30015, 1 },   // Lure of Ballad
        { 30101, 1 },   // Lightning Bolt
        { 30102, 1 },   // Thunder
        { 30201, 1 },   // Firebolt
        { 30202, 1 },   // Fireball
        { 30205, 1 },   // Meteor Strike
        { 30301, 1 },   // Icebolt
        { 30302, 1 },   // Ice Spear
        { 30307, 1 },   // Hailstorm
        { 30401, 1 },   // Ego Wand Incarnate
        { 35002, 1 },   // Life Drain
        { 35004, 1 },   // Water Cannon
        { 35007, 1 },   // Wind Blast
        { 35008, 1 },   // Flamer
        { 35009, 1 },   // Sand Burst
        { 35011, 1 },   // Frozen Blast
        { 35013, 1 },   // Spark
        { 35101, 1 },   // Ego Cylinder Incarnate
        { 41002, 1 },   // Control of Darkness
        { 43002, 1 },   // Elven Magic Missile
        { 44002, 1 },   // Giant Full Swing
        { 45005, 1 },   // Spear of Light
        { 45009, 1 },   // Rage of Wings
        { 46002, 1 },   // Celestial Spike
        { 46003, 1 },   // Judgment Blade
        { 46004, 1 },   // Divine Link
        { 46006, 1 },   // Divine Impact
        { 46008, 1 },   // Ruin of Nova
        { 50075, 1 },   // Glove Throwing (snow)
        { 50180, 1 },   // Pet: Fairy's Magical Dust
        { 50181, 1 },   // Pet: Healing Breeze
        { 50201, 1 },   // Cocopo Blow
        { 52032, 1 },   // Fanaticism
        { 52036, 1 },   // Albangolem Ice Slip
        { 52046, 1 },   // Shooting Star
        { 52073, 1 },   // Purgatory
        { 52096, 1 },   // Dark Bolt
        { 52500, 1 },   // Stop There
        { 53001, 1 },   // Melody Shock
        { 53002, 2 },   // Encore
        { 54101, 1 },   // Act 2: Angry Rush
        { 54102, 1 },   // Act 1: Accidental Crash
        { 54103, 2 },   // Act 4: Jealousy Incarnate
        { 54104, 2 },   // Act 6: Temptational Trap
        { 54151, 1 },   // Act 2: Angry Rush (AI)
        { 54152, 1 },   // Act 1: Accidental Crash (AI)
        { 54153, 1 },   // Act 4: Jealousy Incarnate (AI)
        { 54154, 1 },   // Act 6: Temptational Trap (AI)
        { 54201, 1 },   // Wire Pulling
        { 54202, 1 },   // Wire Binding
        { 54302, 1 },   // Dual Gun Mastery
        { 54303, 1 },   // Cross Buster
        { 54304, 1 },   // Closer
        { 54305, 1 },   // Far Away
        { 54306, 2 },   // Shooting Rush
        { 54307, 2 },   // Bullet Storm
        { 60002, 1 },   // Unlimited Blade Works
        { 60003, 1 },   // Caladbolg 2
        { 60005, 1 },   // Gate of Babylon
        { 65007, 2 },   // Super Temptational Trap
    };

    static int skillTarget(uint16_t skill) {
        for (auto& p : g_skillTargets) {
            if (p.skill == skill) {
                return p.type;
            }
        }

        return 0;
    }

    // mint::CMessage passed by value: vtable, reading offset, shared data.
    struct RawMessage {
        uint32_t words[3];
    };

    using CtorFn = void(__fastcall*)(void* msg, void* edx, uint32_t op, uint64_t id);
    using CtorOpFn = void(__fastcall*)(void* msg, void* edx, uint32_t op);
    using WriteU16Fn = void*(__fastcall*)(void* msg, void* edx, uint16_t value);
    using WriteU64Fn = void*(__fastcall*)(void* msg, void* edx, uint64_t value);
    using CopyFn = void(__fastcall*)(void* dst, void* edx, const void* src);
    using DtorFn = void(__fastcall*)(void* msg, void* edx);
    using SendFn = bool(__fastcall*)(void* vm, void* edx, uint64_t receiver, RawMessage msg);
    using InstanceFn = void*(__cdecl*)();
    using GetProgressFn = uint32_t(__fastcall*)(void* skillMgr, void* edx, uint32_t skill);

    // pleione::CUISkillMgr::UseSkill(u16 skill, u32, CMetaData*, bool): what the skill bar and
    // hotkeys call. __thiscall, returns bool, pops 10h.
    using UseSkillFn = bool(__fastcall*)(void* uiSkillMgr, void* edx, uint32_t skill, uint32_t a2, void* meta, uint32_t a4);

    // pleione::CCharacter::CommandProcessSkill(CMessage): uses the loaded skill now. __thiscall,
    // takes the message by value and destroys it.
    using CommandProcessSkillFn = bool(__fastcall*)(void* character, void* edx, RawMessage msg);

    static CtorFn g_ctor{ nullptr };
    static CtorOpFn g_ctorOp{ nullptr };
    static WriteU16Fn g_writeU16{ nullptr };
    static CommandProcessSkillFn g_commandProcessSkill{ nullptr };
    static WriteU64Fn g_writeU64{ nullptr };
    static CopyFn g_copy{ nullptr };
    static DtorFn g_dtor{ nullptr };
    static SendFn g_send{ nullptr };
    static InstanceFn g_instance{ nullptr };
    static GetProgressFn g_getProgress{ nullptr };

    // Pleione.dll's TSingleton<CInterfaceMgr>::s_pInstanceBlock: points at the interface manager.
    static uintptr_t* g_interfaceMgr{ nullptr };

    static unique_ptr<FunctionHook> g_useSkillHook{};

    // The game's object lookup by id (through TSingleton<CFactory>), as CUISkillMgr::UseSkill finds
    // your character: __stdcall(u64 id) -> object.
    using FindObjectFn = uintptr_t(__stdcall*)(uint64_t id);
    static FindObjectFn g_findObject{ nullptr };

    static uintptr_t findObject(uint64_t id) {
        __try {
            return g_findObject(id);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // CHARACTER's current target id (pleione::CCharacter::GetTargetId reads +0x220, u64), or 0.
    // SEH-guarded.
    static uint64_t currentTarget(uintptr_t character) {
        __try {
            return *(uint64_t*)(character + 0x220);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // Your character's id, as CUISkillMgr::UseSkill gets it: CInterfaceMgr +0xC0 (u64), or 0.
    // SEH-guarded.
    static uint64_t playerID() {
        __try {
            auto mgr = *g_interfaceMgr;

            return mgr != 0 ? *(uint64_t*)(mgr + 0xC0) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }
    static bool g_isEnabled{ false };

    // core::ISkillMgr::GetProgress(SKILL): its progress if it's the loaded skill, else 0. SEH-guarded.
    static uint32_t skillProgress(void* skillMgr, uint16_t skill) {
        __try {
            return g_getProgress(skillMgr, nullptr, skill);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    static void sendAttackTarget(uint64_t ownID, uint64_t targetID) {
        __try {
            alignas(8) uint8_t msg[16]{};

            g_ctor(msg, nullptr, OP_ATTACK_TARGET, ownID);
            g_writeU64(msg, nullptr, targetID);

            // Send takes the message by value and destroys its copy.
            RawMessage copy{};

            g_copy(&copy, nullptr, msg);
            g_send(g_instance(), nullptr, CLIENT_OBJECT, copy);
            g_dtor(msg, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    // Uses CHARACTER's loaded SKILL now on TARGET, as the character controller does once in range:
    // CMessage(4CDh) + u16 skill + u64 target, to CCharacter::CommandProcessSkill.
    static void processSkillNow(uintptr_t character, uint16_t skill, uint64_t targetID) {
        __try {
            alignas(8) uint8_t msg[16]{};

            g_ctorOp(msg, nullptr, 0x4CD);
            g_writeU16(msg, nullptr, skill);
            g_writeU64(msg, nullptr, targetID);

            RawMessage copy{};

            g_copy(&copy, nullptr, msg);
            g_commandProcessSkill((void*)character, nullptr, copy);
            g_dtor(msg, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    // CHARACTER's skill manager (pleione::CCharacter +0x90, core::ISkillMgr*), or 0. SEH-guarded.
    static uintptr_t skillMgrOf(uintptr_t character) {
        __try {
            return *(uintptr_t*)(character + 0x90);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // A skill a press just started loading, to head for its target once it's preparing.
    struct Pending {
        uintptr_t character;
        uint16_t skill;
        uint64_t ownID;
        uint64_t targetID;
        DWORD tick;
    };

    static Pending g_pending{};

    // A press of SKILL. Once it's loading or loaded, heads for your current target (walking into
    // range) and uses it there when it's ready; skills used on yourself go off once loaded. False to
    // let the press load it as usual, in which case a targeted skill heads for the target as soon as
    // it starts preparing.
    static bool castOnTarget(uint16_t skill) {
        g_pending = {};

        auto type = skillTarget(skill);
        auto ownID = playerID();
        auto local = ownID != 0 ? findObject(ownID) : 0;
        auto skillMgr = local != 0 ? skillMgrOf(local) : 0;

        if (type == 0 || skillMgr == 0) {
            return false;
        }

        auto progress = skillProgress((void*)skillMgr, skill);
        auto targetID = type == 2 ? ownID : currentTarget(local);
        auto target = targetID != 0 ? findObject(targetID) : 0;

        if (target == 0) {
            return false;
        }

        // Skills used on yourself (Windmill...) need no target: they go off on whatever's in range.
        if (type == 2) {
            if (progress != PROGRESS_WAITING) {
                return false;
            }

            processSkillNow(local, skill, ownID);

            return true;
        }

        if (progress == PROGRESS_WAITING || progress == PROGRESS_PREPARING) {
            sendAttackTarget(ownID, targetID);

            return true;
        }

        if (progress == 0) {
            g_pending = Pending{ local, skill, ownID, targetID, GetTickCount() };
        }

        return false;
    }

    // Each character update (the game thread): sends your character toward the target of the skill a
    // press just started loading, once it's preparing.
    static void onCharacterUpdate(uintptr_t character) {
        if (g_pending.skill == 0 || character != g_pending.character) {
            return;
        }

        // The skill never started loading (refused, or cancelled).
        if (GetTickCount() - g_pending.tick > 3000) {
            g_pending = {};
            return;
        }

        auto skillMgr = skillMgrOf(character);
        auto progress = skillMgr != 0 ? skillProgress((void*)skillMgr, g_pending.skill) : 0;

        if (progress != PROGRESS_PREPARING && progress != PROGRESS_WAITING) {
            return;
        }

        // Only toward the target the press was aimed at.
        if (currentTarget(character) == g_pending.targetID && findObject(g_pending.targetID) != 0) {
            sendAttackTarget(g_pending.ownID, g_pending.targetID);
        }

        g_pending = {};
    }

    static bool __fastcall hookedUseSkill(void* uiSkillMgr, void* edx, uint32_t skill, uint32_t a2, void* meta, uint32_t a4) {
        auto orig = (UseSkillFn)g_useSkillHook->getOriginal();

        if (g_isEnabled && castOnTarget((uint16_t)skill)) {
            return true;
        }

        return orig(uiSkillMgr, edx, skill, a2, meta, a4);
    }

    SkillHotkey::SkillHotkey()
        : m_isReady{ false }
    {
        log("[SkillHotkey] Entering constructor...");

        auto mint = GetModuleHandleA("Mint.dll");
        auto standard = GetModuleHandleA("Standard.dll");
        auto pleione = GetModuleHandleA("Pleione.dll");

        if (pleione != nullptr) {
            g_interfaceMgr = (uintptr_t*)GetProcAddress(pleione,
                "?s_pInstanceBlock@?$TSingleton@VCInterfaceMgr@pleione@@@esl@@0PAEA");
        }

        if (mint != nullptr) {
            g_ctor = (CtorFn)GetProcAddress(mint, "??0CMessage@mint@@QAE@K_K@Z");
            g_ctorOp = (CtorOpFn)GetProcAddress(mint, "??0CMessage@mint@@QAE@K@Z");
            g_writeU16 = (WriteU16Fn)GetProcAddress(mint, "?WriteU16@CMessage@mint@@QAEAAV12@G@Z");
            g_writeU64 = (WriteU64Fn)GetProcAddress(mint, "?WriteU64@CMessage@mint@@QAEAAV12@_K@Z");
            g_copy = (CopyFn)GetProcAddress(mint, "??0CMessage@mint@@QAE@ABV01@@Z");
            g_dtor = (DtorFn)GetProcAddress(mint, "??1CMessage@mint@@UAE@XZ");
            g_send = (SendFn)GetProcAddress(mint, "?Send@CVirtualMachine@mint@@QAE_N_KVCMessage@2@@Z");
            g_instance = (InstanceFn)GetProcAddress(mint,
                "?GetInstance@?$TSingleton@VCVirtualMachine@mint@@@esl@@SAAAVCVirtualMachine@mint@@XZ");
        }

        if (standard != nullptr) {
            g_getProgress = (GetProgressFn)GetProcAddress(standard, "?GetProgress@ISkillMgr@core@@QBE?AW4ESkillProgress@2@G@Z");
        }

        // CUISkillMgr::UseSkill: push 8Ch; mov eax, <handler>; call _EH_prolog3; mov [ebp-54h], ecx;
        // mov eax, [ebp+10h]; lea ecx, [ebp-44h]; mov [ebp-58h], eax
        auto useSkill = scan("Pleione.dll", "68 8C 00 00 00 B8 ? ? ? ? E8 ? ? ? ? 89 4D AC 8B 45 10 8D 4D BC 89 45 A8");

        // CCharacter::CommandProcessSkill: push 10h; mov eax, <handler>; call _EH_prolog3; mov edi, ecx;
        // mov eax, [edi]; and [ebp-4], 0; call [eax+208h]; test al, al; jz ...
        auto processSkill = scan("Pleione.dll",
            "6A 10 B8 ? ? ? ? E8 ? ? ? ? 8B F9 8B 07 83 65 FC 00 FF 90 08 02 00 00 84 C0 0F 84 92 00 00 00");

        if (processSkill) {
            g_commandProcessSkill = (CommandProcessSkillFn)*processSkill;
        }

        auto messages = g_ctor && g_ctorOp && g_writeU16 && g_writeU64 && g_copy && g_dtor && g_send && g_instance;

        if (!messages || !g_getProgress || !g_interfaceMgr || !useSkill || !g_commandProcessSkill) {
            log("[SkillHotkey] Failed to find everything needed (messages %d, progress %d, interface %d, use skill %d, "
                "process skill %d)", messages, g_getProgress != nullptr, g_interfaceMgr != nullptr, (bool)useSkill,
                g_commandProcessSkill != nullptr);
            log("[SkillHotkey] Leaving constructor");
            return;
        }

        // UseSkill +5Eh: call <find object> (E8 rel32), right after pushing your character's id.
        auto findCall = *useSkill + 0x5E;

        if (*(uint8_t*)findCall != 0xE8) {
            log("[SkillHotkey] UseSkill doesn't look as expected");
            log("[SkillHotkey] Leaving constructor");
            return;
        }

        g_findObject = (FindObjectFn)(findCall + 5 + *(int32_t*)(findCall + 1));

        log("[SkillHotkey] Found UseSkill %p, find object %p", *useSkill, g_findObject);

        g_useSkillHook = make_unique<FunctionHook>(*useSkill, (uintptr_t)&hookedUseSkill);
        m_isReady = g_useSkillHook->isValid() && addCharacterUpdateCallback(&onCharacterUpdate);

        if (!m_isReady) {
            log("[SkillHotkey] Failed to hook UseSkill or character updates");
        }

        log("[SkillHotkey] Leaving constructor");
    }

    void SkillHotkey::onUI() {
        if (ImGui::CollapsingHeader(getName().c_str())) {
            if (!m_isReady) {
                ImGui::TextColored(ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }, "Unavailable: the skill functions weren't found.");
                return;
            }

            ImGui::Checkbox("Enabled##SkillHotkey", &g_isEnabled);
            ImGui::TextWrapped(
                "Use a skill with a target to load it and head for the target while it loads; use it again "
                "to keep heading there and use it once it's loaded and in range. Skills used on yourself, "
                "like Windmill, go off in place when used again once loaded. Skills without a target "
                "(Defense, Counter, Healing...) just load as usual.");
        }
    }

    void SkillHotkey::onConfigLoad(const Config& cfg) {
        g_isEnabled = cfg.get<bool>("SkillHotkey.Enabled").value_or(false);
    }

    void SkillHotkey::onConfigSave(Config& cfg) {
        cfg.set<bool>("SkillHotkey.Enabled", g_isEnabled);
    }
}
