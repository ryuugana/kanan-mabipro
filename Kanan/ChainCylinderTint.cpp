#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

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
        constexpr uintptr_t getConditionMgr = 0xA8;
        constexpr uintptr_t sticker = 0x0C;
        constexpr uintptr_t conditionBalloon = 0x70;
        constexpr uintptr_t balloonConditions = 0xE8;
    }

    // Chain Cylinder skill from skilldb: SkillID 35015 (0x88C7).
    constexpr uint32_t chainCylinderSkill = 35015; // 0x88C7

    using SetColor = void(__thiscall*)(uintptr_t renderEntry, uint32_t rawColor);
    using IsTurnedOnFn = bool(__thiscall*)(void* skillMgr, uint16_t skillID);

    static IsTurnedOnFn g_isTurnedOn{ nullptr };

    using GetPointer = uintptr_t(__thiscall*)(uintptr_t object);

    // esl::CStringT<wchar_t>: a single reference-counted pointer.
    struct EslString {
        void* data;
    };

    // stlport vector of 64-bit condition flag words (IDs 64 * index to 64 * index + 63).
    struct FlagVector {
        uint64_t* begin;
        uint64_t* end;
        uint64_t* capacity;
    };

    using StringConstruct = EslString*(__thiscall*)(EslString* string, const wchar_t* text);
    using StringDestruct = void(__thiscall*)(EslString* string);
    using ConditionDescFind = const void*(__thiscall*)(uintptr_t descMgr, uint32_t condition);
    using ConditionLoadXml = bool(__thiscall*)(uintptr_t descMgr, const EslString* path);
    using ConditionGetFlag = uint64_t(__thiscall*)(uintptr_t conditionMgr, uint32_t index);
    using FileLoad = const uint8_t*(__thiscall*)(uintptr_t fileSystem, const EslString* path, uint32_t* size, void* desc);
    using FileFree = void(__thiscall*)(uintptr_t fileSystem, const uint8_t* data);
    // Render entry: rebuilds the character's condition icons from a set of condition flags.
    using ShowConditions = void(__thiscall*)(uintptr_t renderEntry, const FlagVector* flags);

    static StringConstruct g_stringConstruct{ nullptr };
    static StringDestruct g_stringDestruct{ nullptr };
    static ConditionDescFind g_conditionFind{ nullptr };
    static ConditionLoadXml g_conditionLoadXml{ nullptr };
    static ConditionGetFlag g_conditionGetFlag{ nullptr };
    static ShowConditions g_showConditions{ nullptr };
    static uintptr_t g_conditionDescMgr{ 0 };
    static FileLoad g_fileLoad{ nullptr };
    static FileFree g_fileFree{ nullptr };
    static uintptr_t g_fileSystem{ 0 };

    // The Chain Cylinder condition. The client knows 161 conditions (0-160); this is the next one.
    constexpr uint32_t chainCylinderCondition = 162;
    constexpr size_t flagWords = chainCylinderCondition / 64 + 1;

    // The Chain Cylinder condition's definition, written to the data folder at startup. Modeled on Deadly.
    // no duration, no tint. Its icon is cell (0, 0) of gui_condition_chain_cylinder.dds, which is made
    // from the Chain Cylinder skill's icon.
    constexpr auto conditionFile = L"data/db/CharacterCondition_ChainCylinder.xml";
    constexpr auto conditionXml =
        "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-16\"?>\r\n"
        "<CharacterCondition>\r\n"
        "\t<CharacterConditionList>\r\n"
        "\t\t<CharacterCondition ConditionID=\"162\" Type=\"0\" Priority=\"0\" ConditionEngName=\"Chain Cylinder\" "
        "ConditionLocalName=\"Chain Cylinder\" ImageFile=\"data/gfx/image/gui_condition_chain_cylinder.dds\" "
        "PositionX=\"1\" PositionY=\"0\" DurationType=\"1\" DefaultDurationTime=\"0\" Color=\"00000000\" />\r\n"
        "\t</CharacterConditionList>\r\n"
        "</CharacterCondition>";

    constexpr auto iconFile = "data/gfx/image/gui_condition_chain_cylinder.dds";
    // The Chain Cylinder skill's icon: skillinfo SkillID 35015, cell (0, 0) of 32x32 cells.
    // Adjust skillIconX / skillIconY if the skill icon cell is different.
    constexpr auto skillIconFile = L"data/gfx/image/gui_icon_skill_001.dds";
    constexpr uint32_t skillIconX = 4 * 32;
    constexpr uint32_t skillIconY = 12 * 32;
    // Condition icons are 16x16 cells of a 256x128 A1R5G5B5 image.
    constexpr uint32_t iconWidth = 256;
    constexpr uint32_t iconHeight = 128;
    constexpr uint32_t iconCellX = 1 * 16;

    // CConditionDescMgr::LoadXML drops conditions with IDs of 161 and up; the offset of that limit
    // (cmp dword ptr [ebp+68h], 161) into the function.
    constexpr uintptr_t loadXmlLimitOffset = 0x590 + 3;

    // Sets the loader's condition limit, if it is the one expected.
    static bool setLoadLimit(uint8_t* limit, uint8_t from, uint8_t to) {
        DWORD oldProtect{};

        if (*limit != from || !VirtualProtect(limit, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        *limit = to;
        VirtualProtect(limit, 1, oldProtect, &oldProtect);

        return true;
    }

    // Reads a file the way the client does (data folder or package files).
    static bool readGameFile(const wchar_t* file, std::vector<uint8_t>& out) {
        EslString path{};
        uint32_t size{};
        const uint8_t* data{ nullptr };

        __try {
            g_stringConstruct(&path, file);
            data = g_fileLoad(g_fileSystem, &path, &size, nullptr);
            g_stringDestruct(&path);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        if (data == nullptr) {
            return false;
        }

        out.assign(data, data + size);

        __try {
            g_fileFree(g_fileSystem, data);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        return true;
    }

    // Makes the Chain Cylinder condition's icon from the Chain Cylinder skill's (32x32) icon, halved to the
    // 16x16 condition icons use, in the pixel format of the client's condition icons (A1R5G5B5).
    static bool makeConditionIcon(const std::vector<uint8_t>& skillIcons, std::vector<uint8_t>& out) {
        constexpr size_t headerSize = 128;

        if (skillIcons.size() < headerSize || memcmp(skillIcons.data(), "DDS ", 4) != 0) {
            return false;
        }

        uint32_t height{}, width{}, bits{}, red{}, green{}, blue{}, alpha{};

        memcpy(&height, &skillIcons[12], 4);
        memcpy(&width, &skillIcons[16], 4);
        memcpy(&bits, &skillIcons[88], 4);
        memcpy(&red, &skillIcons[92], 4);
        memcpy(&green, &skillIcons[96], 4);
        memcpy(&blue, &skillIcons[100], 4);
        memcpy(&alpha, &skillIcons[104], 4);

        if (bits != 16 || red != 0x7C00 || green != 0x3E0 || blue != 0x1F || alpha != 0x8000 ||
            width < skillIconX + 32 || height < skillIconY + 32 || skillIcons.size() < headerSize + width * height * 2)
        {
            return false;
        }

        auto source = [&](uint32_t x, uint32_t y) {
            uint16_t pixel{};

            memcpy(&pixel, &skillIcons[headerSize + ((skillIconY + y) * width + skillIconX + x) * 2], 2);
            return pixel;
        };

        std::vector<uint16_t> pixels(iconWidth * iconHeight, 0);

        // Average the opaque pixels of each 2x2 block; a block is opaque when half of it is.
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                uint32_t r{}, g{}, b{}, opaque{};

                for (uint32_t dy = 0; dy < 2; ++dy) {
                    for (uint32_t dx = 0; dx < 2; ++dx) {
                        auto pixel = source(x * 2 + dx, y * 2 + dy);

                        if (pixel & 0x8000) {
                            r += (pixel >> 10) & 31;
                            g += (pixel >> 5) & 31;
                            b += pixel & 31;
                            ++opaque;
                        }
                    }
                }

                if (opaque >= 2) {
                    pixels[y * iconWidth + iconCellX + x] = (uint16_t)(0x8000 | (r / opaque) << 10 | (g / opaque) << 5 | (b / opaque));
                }
            }
        }

        // Same header as the skill icons, resized; no mipmaps.
        out.assign(skillIcons.begin(), skillIcons.begin() + headerSize);

        uint32_t pitch = iconWidth * 2, mipmaps = 0;

        memcpy(&out[12], &iconHeight, 4);
        memcpy(&out[16], &iconWidth, 4);
        memcpy(&out[20], &pitch, 4);
        memcpy(&out[28], &mipmaps, 4);
        out.insert(out.end(), (uint8_t*)pixels.data(), (uint8_t*)(pixels.data() + pixels.size()));

        return true;
    }

    // Writes a file under the client's folder unless it already holds exactly this.
    static bool writeDataFile(const std::filesystem::path& file, const void* data, size_t size) {
        std::error_code error{};

        if (std::filesystem::exists(file, error) && std::filesystem::file_size(file, error) == size) {
            std::ifstream in{ file, std::ios::binary };
            std::vector<char> existing(size);

            if (in.read(existing.data(), size) && memcmp(existing.data(), data, size) == 0) {
                return true;
            }
        }

        std::filesystem::create_directories(file.parent_path(), error);

        std::ofstream out{ file, std::ios::binary | std::ios::trunc };

        return out.write((const char*)data, size).good();
    }

    // The Chain Cylinder condition's files, made from the client's own data: its definition and icon.
    static bool writeConditionFiles() {
        wchar_t clientPath[MAX_PATH]{};

        GetModuleFileNameW(nullptr, clientPath, MAX_PATH);

        auto root = std::filesystem::path{ clientPath }.parent_path();
        std::vector<uint8_t> skillIcons{}, icon{};

        if (!readGameFile(skillIconFile, skillIcons) || !makeConditionIcon(skillIcons, icon)) {
            log("[ChainCylinderTint] Couldn't make the Chain Cylinder condition icon from the skill icons");
            return false;
        }

        return writeDataFile(root / iconFile, icon.data(), icon.size()) &&
            writeDataFile(root / conditionFile, conditionXml, strlen(conditionXml));
    }

    // Whether the client knows the Chain Cylinder condition.
    static bool isChainCylinderConditionDefined() {
        __try {
            return g_conditionFind(g_conditionDescMgr, chainCylinderCondition) != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool loadConditionXml(const wchar_t* file) {
        __try {
            EslString path{};

            g_stringConstruct(&path, file);

            auto result = g_conditionLoadXml(g_conditionDescMgr, &path);

            g_stringDestruct(&path);

            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Loads the Chain Cylinder condition with the client's own loader (the one it loads
    // data/db/CharacterCondition.xml with), letting it through the loader's limit just for this.
    // The loader's original limit is 161 (conditions 0-160 valid); we raise it to chainCylinderCondition + 1
    // so condition 162 passes through. The `from` argument must be the current byte value (161), not the
    // condition ID (162).
    static bool loadChainCylinderCondition() {
        auto limit = (uint8_t*)((uintptr_t)g_conditionLoadXml + loadXmlLimitOffset);

        if (!setLoadLimit(limit, 161, chainCylinderCondition + 1)) {
            log("[ChainCylinderTint] The condition loader isn't the one expected");
            return false;
        }

        auto result = loadConditionXml(conditionFile);

        setLoadLimit(limit, chainCylinderCondition + 1, chainCylinderCondition);

        return result;
    }

    // Kept free of C++ objects so the SEH guard can protect them.
    static uintptr_t getConditionMgr(uintptr_t character) {
        __try {
            auto get = (GetPointer)(*(uintptr_t**)character)[offsets::getConditionMgr / sizeof(uintptr_t)];

            return get(character);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // Whether the character's condition icons include the Chain Cylinder condition.
    static bool isConditionShown(uintptr_t renderEntry) {
        __try {
            auto sticker = *(uintptr_t*)(renderEntry + offsets::sticker);

            if (sticker == 0) {
                return false;
            }

            auto balloon = *(uintptr_t*)(sticker + offsets::conditionBalloon);

            if (balloon == 0) {
                return false;
            }

            auto list = *(uint32_t***)(balloon + offsets::balloonConditions);

            if (list == nullptr) {
                return false;
            }

            for (auto it = list[0]; it != list[1]; ++it) {
                if (*it == chainCylinderCondition) {
                    return true;
                }
            }

            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Rebuilds the character's condition icons from its own conditions, plus Chain Cylinder if asked.
    static bool showConditions(uintptr_t conditionMgr, uintptr_t renderEntry, bool withChainCylinder) {
        __try {
            uint64_t words[flagWords]{};

            for (uint32_t i = 0; i < flagWords; ++i) {
                words[i] = g_conditionGetFlag(conditionMgr, i);
            }

            if (withChainCylinder) {
                words[chainCylinderCondition / 64] |= 1ull << (chainCylinderCondition % 64);
            }

            FlagVector flags{ words, words + flagWords, words + flagWords };

            g_showConditions(renderEntry, &flags);

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

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
        m_showCondition{ false },
        m_isReady{ false },
        m_isConditionLoaded{ false },
        m_mutex{},
        m_characters{},
        m_originalSetColor{}
    {
        g_chainCylinderTint = this;

        auto standard = GetModuleHandleA("Standard.dll");

        if (standard != nullptr) {
            // Resolve IsTurnedOn from Standard.dll (core::ISkillMgr::IsTurnedOn)
            // at runtime so the mod works across reloads.
            // Symbol Standard.core::ISkillMgr::IsTurnedOn resolves to 0x100ED61C;
            // Standard.dll base is 0x10000000, so the offset is 0xED61C.
            auto isTurnedOn = (uint8_t*)standard + 0xED61C;
            if (isTurnedOn) {
                typedef bool(__thiscall* IsTurnedOnFn_t)(void*, uint16_t);
                g_isTurnedOn = (IsTurnedOnFn_t)isTurnedOn;
            }

            g_conditionDescMgr = (uintptr_t)GetProcAddress(standard, "?g_cConditionMgrBlock@core@@3PAEA");
            g_conditionFind = (ConditionDescFind)GetProcAddress(standard,
                "?Find@CConditionDescMgr@core@@QAEPBUSConditionDesc@2@W4ECharacterCondition@@@Z");
            g_conditionLoadXml = (ConditionLoadXml)GetProcAddress(standard,
                "?LoadXML@CConditionDescMgr@core@@AAE_NABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@@Z");
            g_conditionGetFlag = (ConditionGetFlag)GetProcAddress(standard, "?GetFlag@IConditionMgr@core@@QBE_KK@Z");
        }

        if (auto esl = GetModuleHandleA("ESL.dll")) {
            g_stringConstruct = (StringConstruct)GetProcAddress(esl,
                "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@PB_W@Z");
            g_stringDestruct = (StringDestruct)GetProcAddress(esl,
                "??1?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@XZ");
            g_fileSystem = (uintptr_t)GetProcAddress(esl, "?g_cFileSystemBlock@esl@@3PAEA");
            g_fileLoad = (FileLoad)GetProcAddress(esl,
                "?LoadImmediate@CFileSystem@esl@@QAEPBEABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@2@AAKPAUfile_desc@2@@Z");
            g_fileFree = (FileFree)GetProcAddress(esl, "?FreeImmediate@CFileSystem@esl@@QAEXPBE@Z");
        }

        if (auto showConditions = scan("Pleione.dll", "8B 41 0C 85 C0 74 08 8B 48 70")) {
            g_showConditions = (ShowConditions)*showConditions;
        }

        if (g_conditionDescMgr != 0 && g_conditionFind != nullptr && g_conditionLoadXml != nullptr &&
            g_conditionGetFlag != nullptr && g_stringConstruct != nullptr && g_stringDestruct != nullptr &&
            g_showConditions != nullptr && g_fileSystem != 0 && g_fileLoad != nullptr && g_fileFree != nullptr)
        {
            if (!writeConditionFiles()) {
                log("[ChainCylinderTint] Failed to write the Chain Cylinder condition's files to the data folder");
            }

            if (!isChainCylinderConditionDefined()) {
                auto loaded = loadChainCylinderCondition();

                log("[ChainCylinderTint] Loading %ls %s", conditionFile, loaded ? "succeeded" : "failed");
            }

            m_isConditionLoaded = isChainCylinderConditionDefined();
            log("[ChainCylinderTint] Chain Cylinder condition (%u) %s", chainCylinderCondition, m_isConditionLoaded ? "is loaded" : "isn't loaded");
        }
        else {
            log("[ChainCylinderTint] Failed to find the condition functions, the Chain Cylinder condition is unavailable");
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

        for (auto& [vtable, original] : m_originalSetColor) {
            auto slot = (uintptr_t*)(vtable + offsets::setColor);
            DWORD oldProtect{};

            if (VirtualProtect(slot, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                *slot = original;
                VirtualProtect(slot, sizeof(uintptr_t), oldProtect, &oldProtect);
            }
        }

        g_chainCylinderTint = nullptr;
    }

    void ChainCylinderTint::onUI() {
        if (!m_isReady) {
            return;
        }

        if (ImGui::TreeNode(getName().c_str())) {
            ImGui::TextWrapped("Tints characters that have Chain Cylinder (35015) enabled with a custom color.");
            ImGui::Spacing();
            ImGui::Checkbox("Enabled##ChainCylinderTint", &m_isEnabled);
            ImGui::ColorEdit3("Color##ChainCylinderTint", &m_color.x);
            ImGui::SliderFloat("Strength##ChainCylinderTint", &m_strength, 0.0f, 1.0f, "%.2f");

            if (m_isConditionLoaded) {
                ImGui::Spacing();
                ImGui::Checkbox("Show Chain Cylinder condition##ChainCylinderTint", &m_showCondition);
                ImGui::TextDisabled("Shows the Chain Cylinder condition icon with the other condition icons while active.");
            }

			ImGui::TreePop();
        }
    }

    void ChainCylinderTint::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("ChainCylinderTint.Enabled").value_or(false);
        m_color = ImGui::ColorConvertU32ToFloat4(cfg.get<unsigned int>("ChainCylinderTint.Color").value_or(IM_COL32(255, 140, 38, 255)));
        m_strength = cfg.get<float>("ChainCylinderTint.Strength").value_or(1.0f);
        m_showCondition = cfg.get<bool>("ChainCylinderTint.ShowCondition").value_or(false);
    }

    void ChainCylinderTint::onConfigSave(Config& cfg) {
        cfg.set<bool>("ChainCylinderTint.Enabled", m_isEnabled);
        cfg.set<unsigned int>("ChainCylinderTint.Color", ImGui::ColorConvertFloat4ToU32(m_color));
        cfg.set<float>("ChainCylinderTint.Strength", m_strength);
        cfg.set<bool>("ChainCylinderTint.ShowCondition", m_showCondition);
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
        // Off, with no tint or condition left to take back off: nothing to do for this character.
        if (!m_isEnabled && !m_showCondition) {
            scoped_lock<mutex> _{ m_mutex };

            if (m_tintedCount == 0 && m_conditionShown.empty()) {
                return;
            }
        }

        CharacterState state{};

        if (!readCharacterState(character, state)) {
            return;
        }

        scoped_lock<mutex> _{ m_mutex };

        updateCondition(character, state.renderEntry, state.chainCylinderOn);

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

    // Shows the Chain Cylinder condition with the character's condition icons while it is active. The
    // client rebuilds the icons whenever the server sends the character's conditions, which drops
    // it, so it is put back whenever it is missing.
    void ChainCylinderTint::updateCondition(uintptr_t character, uintptr_t renderEntry, bool chainCylinderOn) {
        if (!m_isConditionLoaded) {
            return;
        }

        auto shouldShow = m_showCondition && chainCylinderOn;
        auto wasShown = m_conditionShown.count(renderEntry) != 0;

        if (!shouldShow && !wasShown) {
            return;
        }

        auto conditionMgr = getConditionMgr(character);

        if (conditionMgr == 0) {
            return;
        }

        if (shouldShow) {
            if (!isConditionShown(renderEntry) && showConditions(conditionMgr, renderEntry, true)) {
                m_conditionShown.insert(renderEntry);
            }
        }
        else {
            showConditions(conditionMgr, renderEntry, false);
            m_conditionShown.erase(renderEntry);
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

