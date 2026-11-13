#include <cstddef>
#include <cstdio>
#include <vector>

#include <imgui.h>

#include <Scan.hpp>

#include "BiggerLRodLights.hpp"
#include "Log.hpp"

namespace kanan {
    // The light value the L-Rod's vertex modifier reads (+0x18 of the detection state). It becomes
    // the per-vertex glow, so multiplying it makes the light brighter.
    static float g_lrodLightScale{ 1.0f };
    static uintptr_t g_lrodLightReturn{ 0 };

    // Scale applied to the glow mesh's vertices. Read on every call, so the slider takes effect
    // immediately.
    static float g_lrodMeshScale{ 3.0f };
    static uintptr_t g_lrodMeshResume{ 0 };

    // Vertices of the buffer the modifier was last given, and the positions they started with. The
    // game hands the modifier the same buffer every frame, so the scale is applied to these saved
    // positions rather than to whatever is in the buffer now (which would compound every frame).
    static void* g_lrodMeshArray{ nullptr };
    static int g_lrodMeshCount{ 0 };
    static std::vector<float> g_lrodMeshPositions;
    static float g_lrodMeshCentre[3]{ 0.0f, 0.0f, 0.0f };

    // Replaces the "return the light value" path of the value getter:
    //     fld dword ptr [esi+18h] / jmp +2   ->   fld [esi+18h]; fmul [brightness]; jmp back
    // ESI holds the detection state here (the getter set it from ECX on entry), and the value is
    // left in st(0), where the caller expects it. The expired path's "fldz" is left alone.
    static __declspec(naked) void hookLRodLightValue() {
        __asm {
            fld     dword ptr [esi + 18h]
            fmul    dword ptr [g_lrodLightScale]
            jmp     g_lrodLightReturn
        }
    }

    // Grows the light by scaling the positions of the vertices the L-Rod's vertex modifier is given,
    // around the middle of the mesh so the light expands where it is instead of drifting away from
    // the rod. Vertices are 0x18 bytes: x, y, z, the glow value the game fills in, then UVs.
    static void __stdcall scaleLRodGlowMesh(void* object, void* vertices, int vertexCount) {
        const auto scale = g_lrodMeshScale;

        if (object == nullptr || vertices == nullptr || vertexCount <= 0 || vertexCount > 200000 || scale == 1.0f) {
            return;
        }

        auto v = static_cast<float*>(vertices);
        auto count = static_cast<size_t>(vertexCount);

        if (vertices != g_lrodMeshArray || vertexCount != g_lrodMeshCount) {
            g_lrodMeshPositions.resize(count * 3);

            for (size_t i = 0; i < count; ++i) {
                g_lrodMeshPositions[i * 3 + 0] = v[i * 6 + 0];
                g_lrodMeshPositions[i * 3 + 1] = v[i * 6 + 1];
                g_lrodMeshPositions[i * 3 + 2] = v[i * 6 + 2];
            }

            g_lrodMeshCentre[0] = 0.0f;
            g_lrodMeshCentre[1] = 0.0f;
            g_lrodMeshCentre[2] = 0.0f;

            for (size_t i = 0; i < count; ++i) {
                g_lrodMeshCentre[0] += g_lrodMeshPositions[i * 3 + 0];
                g_lrodMeshCentre[1] += g_lrodMeshPositions[i * 3 + 1];
                g_lrodMeshCentre[2] += g_lrodMeshPositions[i * 3 + 2];
            }

            g_lrodMeshCentre[0] /= (float)count;
            g_lrodMeshCentre[1] /= (float)count;
            g_lrodMeshCentre[2] /= (float)count;

            g_lrodMeshArray = vertices;
            g_lrodMeshCount = vertexCount;
        }

        for (size_t i = 0; i < count; ++i) {
            for (int c = 0; c < 3; ++c) {
                const auto original = g_lrodMeshPositions[i * 3 + c];
                v[i * 6 + c] = g_lrodMeshCentre[c] + (original - g_lrodMeshCentre[c]) * scale;
            }
        }
    }

    // Replaces the start of pleione::lrod_vertex_modifier::get_values:
    //     push ebp / mov ebp, esp / mov eax, [ebp+8]
    // with the same prologue plus the scaling, then carries on at the "test eax, eax" that follows.
    static __declspec(naked) void hookLRodGlowMesh() {
        __asm {
            push    ebp
            mov     ebp, esp
            push    dword ptr [ebp + 10h]
            push    dword ptr [ebp + 0Ch]
            push    dword ptr [ebp + 8]
            call    scaleLRodGlowMesh
            mov     eax, dword ptr [ebp + 8]
            jmp     g_lrodMeshResume
        }
    }

    BiggerLRodLights::BiggerLRodLights()
        : PatchMod{ "Bigger L-Rod Lights", "Makes the light the L-Rod gives off when it detects an artifact much bigger." },
        m_enabled{ false },
        m_isAvailable{ false },
        m_size{ 3.0f },
        m_brightness{ 1.0f },
        m_brightnessPatch{},
        m_sizePatch{}
    {
        log("[BiggerLRodLights] Entering constructor...");

        // The light value getter, where it returns the value while the light is still lit:
        //   cmp edi, eax / ja +5 / fld dword ptr [esi+18h] / jmp +2 / fldz / pop edi / pop esi / ret
        // The "ja" and the "fldz" are the two ways out of the time check, so this only matches this
        // one getter, and the expired path's "fldz" is outside the bytes we replace.
        auto valueSite = scan("Pleione.dll", "3B F8 77 05 D9 46 18 EB 02 D9 EE 5F 5E C3");

        // The L-Rod's vertex modifier, at the top of get_values:
        //   push ebp / mov ebp, esp / mov eax,[ebp+8] / test eax,eax / jz ... /
        //   push edi / mov edi,[ebp+0Ch] / test edi,edi / jz ... / push ebx ...
        auto meshSite = scan("Pleione.dll",
            "55 8B EC 8B 45 08 85 C0 0F 84 80 00 00 00 57 8B 7D 0C 85 FF 74 77 53 8B 5D 10 85 DB 74 6E");

        bool ok = true;

        if (!valueSite) {
            log("[BiggerLRodLights] Failed to find the L-Rod light value.");
            ok = false;
        }
        else {
            // 3B F8 cmp edi,eax | 77 05 ja | D9 46 18 fld [esi+18h] | EB 02 jmp | D9 EE fldz ...
            auto fld = *valueSite + 4;      // the "fld" and the "jmp" are exactly the 5 bytes replaced.
            auto resume = *valueSite + 11;  // pop edi, after the fldz the expired reading takes.

            g_lrodLightReturn = resume;

            auto rel = (uintptr_t)&hookLRodLightValue - (fld + 5);

            m_brightnessPatch.address = fld;
            m_brightnessPatch.bytes = { 0xE9, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF) };

            log("[BiggerLRodLights] Found the L-Rod light value at %p", fld);
        }

        if (!meshSite) {
            log("[BiggerLRodLights] Failed to find the L-Rod glow mesh.");
            ok = false;
        }
        else {
            // push ebp / mov ebp, esp / mov eax,[ebp+8] is 8 bytes; carry on at "test eax, eax".
            auto site = *meshSite;
            auto resume = site + 6;

            g_lrodMeshResume = resume;

            auto rel = (uintptr_t)&hookLRodGlowMesh - (site + 5);

            m_sizePatch.address = site;
            m_sizePatch.bytes = { 0xE9, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF), (int16_t)((rel >> 24) & 0xFF) };

            log("[BiggerLRodLights] Found the L-Rod glow mesh at %p", site);
        }

        m_isAvailable = ok;

        log("[BiggerLRodLights] Leaving constructor.");
    }

    void BiggerLRodLights::apply() {
        if (!m_isAvailable) {
            return;
        }

        g_lrodLightScale = m_brightness;
        g_lrodMeshScale = m_size;

        log("[BiggerLRodLights] Toggling %s", m_enabled ? "on" : "off");

        if (m_enabled) {
            patch(m_brightnessPatch);
            patch(m_sizePatch);
        }
        else {
            undoPatch(m_brightnessPatch);
            undoPatch(m_sizePatch);

            // Let the next enable capture the mesh fresh.
            g_lrodMeshArray = nullptr;
            g_lrodMeshCount = 0;
        }
    }

    void BiggerLRodLights::onPatchUI() {
        if (!m_isAvailable) {
            return;
        }

        if (ImGui::Checkbox("Bigger L-Rod Lights", &m_enabled)) {
            apply();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Makes the light the L-Rod gives off when it detects an artifact much bigger.");
        }

        if (m_enabled) {
            // Both are read every time the rod renders, so there is nothing to re-patch on a change.
            if (ImGui::SliderFloat("Light size", &m_size, 1.0f, 10.0f, "%.1fx")) {
                g_lrodMeshScale = m_size;
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How far the light spreads from the rod. 1x is the normal light.");
            }

            if (ImGui::SliderFloat("Light brightness", &m_brightness, 1.0f, 10.0f, "%.1fx")) {
                g_lrodLightScale = m_brightness;
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How bright the light is. 1x is the normal brightness.");
            }
        }
    }

    void BiggerLRodLights::onConfigLoad(const Config& cfg) {
        m_size = cfg.get<float>("BiggerLRodLights.Size").value_or(3.0f);

        // Older builds only had the brightness value, under "Scale".
        m_brightness = cfg.get<float>("BiggerLRodLights.Brightness")
            .value_or(cfg.get<float>("BiggerLRodLights.Scale").value_or(1.0f));

        m_enabled = cfg.get<bool>("BiggerLRodLights.Enabled").value_or(false);

        if (m_enabled) {
            apply();
        }
    }

    void BiggerLRodLights::onConfigSave(Config& cfg) {
        cfg.set<float>("BiggerLRodLights.Size", m_size);
        cfg.set<float>("BiggerLRodLights.Brightness", m_brightness);
        cfg.set<bool>("BiggerLRodLights.Enabled", m_enabled);
    }
}
