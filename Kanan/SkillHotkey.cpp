#include <cwchar>
#include <cwctype>
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

    // What a press does with a skill, decided from the game's own skill data (skillinfo, loaded
    // at startup): attacks aimed at enemies go at your current target; melee skills that go off in
    // place go off where you stand; everything else loads as usual.
    enum CastKind {
        CAST_NONE,
        CAST_TARGET,
        CAST_SELF,
    };

    // core::SSkillDesc lock bits (WaitLock: while loaded; ProcessLock: while going off).
    constexpr uint32_t LOCK_WALK = 0x8;
    constexpr uint32_t LOCK_HIT = 0x20;

    // core::ESkillType of attack skills: melee, ranged, magic, and alchemy (the rebalanced type
    // alchemy attacks like Water Cannon get: skillinfo's SkillTypeRebalance).
    constexpr uint32_t SKILL_MELEE = 1;
    constexpr uint32_t SKILL_RANGED = 2;
    constexpr uint32_t SKILL_MAGIC = 3;
    constexpr uint32_t SKILL_ALCHEMY = 11;

    // Standard.dll's core::CSkillDescMgr and core::SSkillDesc, and ESL.dll's string.
    using SkillDescMgrFn = void*(__cdecl*)();
    using SkillDescReadyFn = bool(__fastcall*)(void* mgr, void* edx);
    using FindSkillDescFn = const void*(__fastcall*)(void* mgr, void* edx, uint32_t skill, uint32_t race);
    using DescStringFn = const void*(__fastcall*)(const void* desc, void* edx);
    using DescValueFn = uint32_t(__fastcall*)(const void* desc, void* edx);
    using DescFlagFn = bool(__fastcall*)(const void* desc, void* edx);
    using StringContentFn = const wchar_t*(__fastcall*)(const void* str, void* edx);

    static SkillDescMgrFn g_skillDescMgr{ nullptr };
    static SkillDescReadyFn g_skillDescReady{ nullptr };
    static FindSkillDescFn g_findSkillDesc{ nullptr };
    static DescStringFn g_targetPreference{ nullptr };
    static DescValueFn g_skillType{ nullptr };
    static DescValueFn g_useType{ nullptr };
    static DescValueFn g_waitLock{ nullptr };
    static DescValueFn g_processLock{ nullptr };
    static DescFlagFn g_isHidden{ nullptr };
    static StringContentFn g_stringContent{ nullptr };

    struct SkillDescInfo {
        wchar_t preference[64];     // TargetPreference, lowercase: "enemy", "enemy|prop(...)", "me & friend"...
        uint32_t type;
        uint32_t useType;
        uint32_t waitLock;
        uint32_t processLock;
        bool hidden;
    };

    // SKILL's data, from the game. False if it isn't loaded or there's no such skill. SEH-guarded.
    static bool readSkillDesc(uint16_t skill, SkillDescInfo& info) {
        __try {
            auto mgr = g_skillDescMgr();

            if (mgr == nullptr || !g_skillDescReady(mgr, nullptr)) {
                return false;
            }

            // Race 0: the human table; every race's copy has the same targeting.
            auto desc = g_findSkillDesc(mgr, nullptr, skill, 0);

            if (desc == nullptr) {
                return false;
            }

            auto preference = g_stringContent(g_targetPreference(desc, nullptr), nullptr);
            size_t n = 0;

            for (; preference != nullptr && preference[n] != 0 && n + 1 < _countof(info.preference); ++n) {
                info.preference[n] = towlower(preference[n]);
            }

            info.preference[n] = 0;
            info.type = g_skillType(desc, nullptr);
            info.useType = g_useType(desc, nullptr);
            info.waitLock = g_waitLock(desc, nullptr);
            info.processLock = g_processLock(desc, nullptr);
            info.hidden = g_isHidden(desc, nullptr);

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Whether TEXT has WORD as a whole word.
    static bool hasWord(const wchar_t* text, const wchar_t* word) {
        auto length = wcslen(word);

        for (auto p = text; *p != 0;) {
            if (!iswalpha(*p)) {
                ++p;
                continue;
            }

            auto start = p;

            while (iswalpha(*p)) {
                ++p;
            }

            if ((size_t)(p - start) == length && wcsncmp(start, word, length) == 0) {
                return true;
            }
        }

        return false;
    }

    static CastKind castKind(uint16_t skill) {
        SkillDescInfo info{};

        // Only active attack skills (not hidden, passive, or toggled).
        if (!readSkillDesc(skill, info) || info.hidden || info.useType != 0) {
            return CAST_NONE;
        }

        if (info.type != SKILL_MELEE && info.type != SKILL_RANGED && info.type != SKILL_MAGIC &&
            info.type != SKILL_ALCHEMY)
        {
            return CAST_NONE;
        }

        // Aimed at enemies only; skills that can also take you or friends (healing...) load as usual.
        if (wcsncmp(info.preference, L"enemy", 5) != 0 || hasWord(info.preference, L"me") ||
            hasWord(info.preference, L"friend"))
        {
            return CAST_NONE;
        }

        // Melee skills that can't walk once loaded can't head for a target. The ones that also can't
        // be hit while they go off spin in place (Windmill); the rest (Stomp...) load as usual.
        if (info.type == SKILL_MELEE && (info.waitLock & LOCK_WALK) != 0) {
            return (info.processLock & LOCK_HIT) != 0 ? CAST_SELF : CAST_NONE;
        }

        return CAST_TARGET;
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

        auto kind = castKind(skill);
        auto ownID = playerID();
        auto local = ownID != 0 ? findObject(ownID) : 0;
        auto skillMgr = local != 0 ? skillMgrOf(local) : 0;

        if (kind == CAST_NONE || skillMgr == 0) {
            return false;
        }

        auto progress = skillProgress((void*)skillMgr, skill);
        auto targetID = kind == CAST_SELF ? ownID : currentTarget(local);
        auto target = targetID != 0 ? findObject(targetID) : 0;

        if (target == 0) {
            return false;
        }

        // Skills used on yourself (Windmill...) need no target: they go off on whatever's in range.
        if (kind == CAST_SELF) {
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
            g_skillDescMgr = (SkillDescMgrFn)GetProcAddress(standard, "?Instance@CSkillDescMgr@core@@SAAAV12@XZ");
            g_skillDescReady = (SkillDescReadyFn)GetProcAddress(standard, "?IsInitialized@CSkillDescMgr@core@@QBE_NXZ");
            g_findSkillDesc = (FindSkillDescFn)GetProcAddress(standard,
                "?Find@CSkillDescMgr@core@@QBEPBUSSkillDesc@2@GW4ERaceType@@@Z");
            g_targetPreference = (DescStringFn)GetProcAddress(standard,
                "?GetTargetPreference@SSkillDesc@core@@QBEABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@XZ");
            g_skillType = (DescValueFn)GetProcAddress(standard, "?GetSkillType@SSkillDesc@core@@QBE?AW4ESkillType@2@XZ");
            g_useType = (DescValueFn)GetProcAddress(standard, "?GetUseType@SSkillDesc@core@@QBE?AW4ESkillUseType@2@XZ");
            g_waitLock = (DescValueFn)GetProcAddress(standard, "?GetWaitLock@SSkillDesc@core@@QBE?BKXZ");
            g_processLock = (DescValueFn)GetProcAddress(standard, "?GetProcessLock@SSkillDesc@core@@QBE?BKXZ");
            g_isHidden = (DescFlagFn)GetProcAddress(standard, "?IsHidden@SSkillDesc@core@@QBE_NXZ");
        }

        if (auto esl = GetModuleHandleA("ESL.dll"); esl != nullptr) {
            g_stringContent = (StringContentFn)GetProcAddress(esl,
                "?GetSafeContent@?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QBEPB_WXZ");
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
        auto skillData = g_skillDescMgr && g_skillDescReady && g_findSkillDesc && g_targetPreference && g_skillType &&
            g_useType && g_waitLock && g_processLock && g_isHidden && g_stringContent;

        if (!messages || !skillData || !g_getProgress || !g_interfaceMgr || !useSkill || !g_commandProcessSkill) {
            log("[SkillHotkey] Failed to find everything needed (messages %d, skill data %d, progress %d, interface %d, "
                "use skill %d, process skill %d)", messages, skillData, g_getProgress != nullptr, g_interfaceMgr != nullptr,
                (bool)useSkill, g_commandProcessSkill != nullptr);
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
