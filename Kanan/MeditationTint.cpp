#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <Scan.hpp>

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
    //   CCharacter vtable +0xA8 -> core::IConditionMgr
    //   render entry +0x0C -> CCharacterSticker (name tag), sticker +0x70 -> CConditionBalloon:
    //     the condition icons (Deadly, ...), built from a character's condition flags; its list of
    //     shown conditions is at +0xE8 (pointer to a vector of condition IDs)
    namespace offsets {
        constexpr uintptr_t renderEntry = 0x198;
        constexpr uintptr_t setColor = 0xB0;
        constexpr uintptr_t getStateMgr = 0x80;
        constexpr uintptr_t getConditionMgr = 0xA8;
        constexpr uintptr_t sticker = 0x0C;
        constexpr uintptr_t conditionBalloon = 0x70;
        constexpr uintptr_t balloonConditions = 0xE8;
    }

    using GetPointer = uintptr_t(__thiscall*)(uintptr_t object);
    using GetBool = bool(__thiscall*)(uintptr_t object);
    using SetColor = void(__thiscall*)(uintptr_t renderEntry, uint32_t rawColor);

    static GetBool g_isMeditating{ nullptr };

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

    // The Meditation condition. The client knows 161 conditions (0-160); this is the next one.
    constexpr uint32_t meditationCondition = 161;
    constexpr size_t flagWords = meditationCondition / 64 + 1;

    // The Meditation condition's definition, written to the data folder at startup. Modeled on Deadly:
    // no duration, no tint. Its icon is cell (1, 0) of gui_condition_meditation.dds, which is made
    // from the Meditation skill's icon (a (0, 0) cell means no icon to the client).
    constexpr auto conditionFile = L"data/db/CharacterCondition_Meditation.xml";
    constexpr auto conditionXml =
        "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-16\"?>\r\n"
        "<CharacterCondition>\r\n"
        "\t<CharacterConditionList>\r\n"
        "\t\t<CharacterCondition ConditionID=\"161\" Type=\"0\" Priority=\"0\" ConditionEngName=\"Meditation\" "
        "ConditionLocalName=\"Meditating\" ImageFile=\"data/gfx/image/gui_condition_meditation.dds\" "
        "PositionX=\"1\" PositionY=\"0\" DurationType=\"1\" DefaultDurationTime=\"0\" Color=\"00000000\" />\r\n"
        "\t</CharacterConditionList>\r\n"
        "</CharacterCondition>";

    constexpr auto iconFile = "data/gfx/image/gui_condition_meditation.dds";
    // The Meditation skill's icon: skillinfo SkillID 30003, cell (7, 2) of 32x32 cells.
    constexpr auto skillIconFile = L"data/gfx/image/gui_icon_skill_000.dds";
    constexpr uint32_t skillIconX = 7 * 32;
    constexpr uint32_t skillIconY = 2 * 32;
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

    // Makes the Meditation condition's icon from the Meditation skill's (32x32) icon, halved to the
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

    // The Meditation condition's files, made from the client's own data: its definition and icon.
    static bool writeConditionFiles() {
        wchar_t clientPath[MAX_PATH]{};

        GetModuleFileNameW(nullptr, clientPath, MAX_PATH);

        auto root = std::filesystem::path{ clientPath }.parent_path();
        std::vector<uint8_t> skillIcons{}, icon{};

        if (!readGameFile(skillIconFile, skillIcons) || !makeConditionIcon(skillIcons, icon)) {
            log("[MeditationTint] Couldn't make the Meditation condition icon from the skill icons");
            return false;
        }

        return writeDataFile(root / iconFile, icon.data(), icon.size()) &&
            writeDataFile(root / conditionFile, conditionXml, strlen(conditionXml));
    }

    // Whether the client knows the Meditation condition.
    static bool isMeditationConditionDefined() {
        __try {
            return g_conditionFind(g_conditionDescMgr, meditationCondition) != nullptr;
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

    // Loads the Meditation condition with the client's own loader (the one it loads
    // data/db/CharacterCondition.xml with), letting it through the loader's limit just for this.
    static bool loadMeditationCondition() {
        auto limit = (uint8_t*)((uintptr_t)g_conditionLoadXml + loadXmlLimitOffset);

        if (!setLoadLimit(limit, meditationCondition, meditationCondition + 1)) {
            log("[MeditationTint] The condition loader isn't the one expected");
            return false;
        }

        auto result = loadConditionXml(conditionFile);

        setLoadLimit(limit, meditationCondition + 1, meditationCondition);

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

    // Whether the character's condition icons include the Meditation condition.
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
                if (*it == meditationCondition) {
                    return true;
                }
            }

            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Rebuilds the character's condition icons from its own conditions, plus Meditation if asked.
    static bool showConditions(uintptr_t conditionMgr, uintptr_t renderEntry, bool withMeditation) {
        __try {
            uint64_t words[flagWords]{};

            for (uint32_t i = 0; i < flagWords; ++i) {
                words[i] = g_conditionGetFlag(conditionMgr, i);
            }

            if (withMeditation) {
                words[meditationCondition / 64] |= 1ull << (meditationCondition % 64);
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
        m_showCondition{ false },
        m_isReady{ false },
        m_isConditionLoaded{ false },
        m_mutex{},
        m_characters{},
        m_originalSetColor{}
    {
        log("[MeditationTint] Entering constructor...");

        g_meditationTint = this;

        auto standard = GetModuleHandleA("Standard.dll");

        if (standard != nullptr) {
            g_isMeditating = (GetBool)GetProcAddress(standard, "?IsMeditating@IStateMgr@core@@QAE_NXZ");
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
                log("[MeditationTint] Failed to write the Meditation condition's files to the data folder");
            }

            if (!isMeditationConditionDefined()) {
                auto loaded = loadMeditationCondition();

                log("[MeditationTint] Loading %ls %s", conditionFile, loaded ? "succeeded" : "failed");
            }

            m_isConditionLoaded = isMeditationConditionDefined();
            log("[MeditationTint] Meditation condition (%u) %s", meditationCondition, m_isConditionLoaded ? "is loaded" : "isn't loaded");
        }
        else {
            log("[MeditationTint] Failed to find the condition functions, the Meditation condition is unavailable");
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

            if (m_isConditionLoaded) {
                ImGui::Spacing();
                ImGui::Checkbox("Show Meditation condition##MeditationTint", &m_showCondition);
                ImGui::TextDisabled("Shows the Meditation condition icon with the other condition icons while meditating.");
            }

            ImGui::TreePop();
        }
    }

    void MeditationTint::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("MeditationTint.Enabled").value_or(false);
        m_color = ImGui::ColorConvertU32ToFloat4(cfg.get<unsigned int>("MeditationTint.Color").value_or(IM_COL32(178, 102, 255, 255)));
        m_strength = cfg.get<float>("MeditationTint.Strength").value_or(1.0f);
        m_showCondition = cfg.get<bool>("MeditationTint.ShowCondition").value_or(false);
    }

    void MeditationTint::onConfigSave(Config& cfg) {
        cfg.set<bool>("MeditationTint.Enabled", m_isEnabled);
        cfg.set<unsigned int>("MeditationTint.Color", ImGui::ColorConvertFloat4ToU32(m_color));
        cfg.set<float>("MeditationTint.Strength", m_strength);
        cfg.set<bool>("MeditationTint.ShowCondition", m_showCondition);
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

        updateCondition(character, state.renderEntry, state.isMeditating);

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

    // Shows the Meditation condition with the character's condition icons while it meditates. The
    // client rebuilds the icons whenever the server sends the character's conditions, which drops
    // it, so it is put back whenever it is missing.
    void MeditationTint::updateCondition(uintptr_t character, uintptr_t renderEntry, bool isMeditating) {
        if (!m_isConditionLoaded) {
            return;
        }

        auto shouldShow = m_showCondition && isMeditating;
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
