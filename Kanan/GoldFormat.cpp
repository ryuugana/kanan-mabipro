#include <Windows.h>

#include <imgui.h>
#include <String.hpp>

#include "Log.hpp"
#include "GoldFormat.hpp"

using namespace std;

namespace kanan {
    static GoldFormat* g_goldFormat{ nullptr };

    // esl::CultureInfo::GetMoneyString(amount, language): builds the text the game shows for an
    // amount of gold into STRING (an esl::CStringT<wchar_t> it constructs), and returns it.
    using GetMoneyString = void*(__cdecl*)(void* string, unsigned long amount, int language);
    using StringConstruct = void*(__thiscall*)(void* string, const wchar_t* text);

    static StringConstruct g_stringConstruct{ nullptr };

    static const char* const g_styleNames[GoldFormat::STYLE_COUNT] = {
        "Game default",
        "Commas (46,500)",
        "Short (46.5k)",
        "Letters (46k500)",
        "Letters with spaces (46k 500)",
    };

    GoldFormat::GoldFormat()
        : m_style{ GAME },
        m_hook{}
    {
        g_goldFormat = this;

        if (auto esl = GetModuleHandleA("ESL.dll")) {
            g_stringConstruct = (StringConstruct)GetProcAddress(esl,
                "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@PB_W@Z");

            auto getMoneyString = GetProcAddress(esl,
                "?GetMoneyString@CultureInfo@esl@@YA?BV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@2@KW4ELanguageId@esl_constant@2@@Z");

            if (getMoneyString != nullptr && g_stringConstruct != nullptr) {
                m_hook = make_unique<FunctionHook>((uintptr_t)getMoneyString, (uintptr_t)&GoldFormat::getMoneyString);
            }
        }

        log("[GoldFormat] %s the gold text", m_hook && m_hook->isValid() ? "Hooked" : "Failed to hook");
    }

    wstring GoldFormat::format(unsigned long amount, Style style) {
        auto millions = amount / 1000000;
        auto thousands = amount / 1000 % 1000;
        auto ones = amount % 1000;

        switch (style) {
        case COMMAS: {
            auto digits = to_wstring(amount);

            for (auto i = (int)digits.size() - 3; i > 0; i -= 3) {
                digits.insert(i, L",");
            }

            return digits;
        }

        case SHORT: {
            // The largest unit, with one decimal, rounded down so an amount is never shown as more
            // than it is: 46,999 is 46.9k.
            auto unit = amount >= 1000000000 ? 1000000000ul : amount >= 1000000 ? 1000000ul : amount >= 1000 ? 1000ul : 1ul;

            if (unit == 1) {
                return to_wstring(amount);
            }

            auto tenths = amount / (unit / 10);
            auto text = to_wstring(tenths / 10);

            if (tenths % 10 != 0) {
                text += L"." + to_wstring(tenths % 10);
            }

            return text + (unit == 1000000000 ? L"b" : unit == 1000000 ? L"m" : L"k");
        }

        case LETTERS:
        case SPACED_LETTERS: {
            // Each part that isn't zero: 1,000,500 is 1m500.
            auto separator = style == SPACED_LETTERS ? L" " : L"";
            wstring text{};

            auto add = [&](unsigned long part, const wchar_t* unit) {
                if (part == 0) {
                    return;
                }

                if (!text.empty()) {
                    text += separator;
                }

                text += to_wstring(part) + unit;
            };

            add(millions, L"m");
            add(thousands, L"k");
            add(ones, L"");

            return text.empty() ? L"0" : text;
        }

        default:
            return to_wstring(amount);
        }
    }

    void* __cdecl GoldFormat::getMoneyString(void* string, unsigned long amount, int language) {
        auto self = g_goldFormat;

        if (self->m_style <= GAME || self->m_style >= STYLE_COUNT) {
            return ((GetMoneyString)self->m_hook->getOriginal())(string, amount, language);
        }

        return g_stringConstruct(string, format(amount, (Style)self->m_style).c_str());
    }

    void GoldFormat::onUI() {
        if (m_hook == nullptr || !m_hook->isValid()) {
            return;
        }

        if (ImGui::TreeNode("Gold Format")) {
            ImGui::TextWrapped("Chooses how amounts of gold are shown in the game, such as in your inventory, shops and "
                "the bank.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

            ImGui::Combo("Format", &m_style, g_styleNames, STYLE_COUNT);

            if (m_style > GAME && m_style < STYLE_COUNT) {
                ImGui::TextWrapped("Examples: %s, %s, %s", narrow(format(46500, (Style)m_style)).c_str(),
                    narrow(format(1234567, (Style)m_style)).c_str(), narrow(format(1000500, (Style)m_style)).c_str());
            }

            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::TextWrapped("Any format other than Game default replaces the Enable Money Letters patch.");
            ImGui::TreePop();
        }
    }

    void GoldFormat::onConfigLoad(const Config& cfg) {
        m_style = cfg.get<int>("GoldFormat.Style").value_or(GAME);

        if (m_style < GAME || m_style >= STYLE_COUNT) {
            m_style = GAME;
        }
    }

    void GoldFormat::onConfigSave(Config& cfg) {
        cfg.set<int>("GoldFormat.Style", m_style);
    }
}
