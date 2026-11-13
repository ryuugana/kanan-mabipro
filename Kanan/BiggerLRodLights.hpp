#pragma once

#include <Patch.hpp>

#include "PatchMod.hpp"

namespace kanan {
    // Makes the L-Rod's exploration light much bigger.
    //
    // How the L-Rod's light works (G13 client, Pleione.dll):
    //
    //   When you use the L-Rod to explore, the client keeps a "detection" state on your character
    //   (character object +0x128 points at it). The state holds
    //     +0x00..  a tag: how strong the reading is, worked out from the distance to the artifact
    //     +0x18    the light value (float) the rod renders with
    //     +0x20/24 when the reading was taken, as a 64-bit time
    //
    //   Two small helpers turn that tag into what you see and hear:
    //     Pleione+0x140020  tag -> 2 / 3 / 4          (how many beeps; at 4 you're on the artifact)
    //     Pleione+0x14003d  tag -> 2000 / 3000 / 4000 (how long the light stays lit, in ms)
    //
    //   and this getter is what the rod's light reads every frame it renders:
    //
    //     Pleione+0x140126  float get_light_value(this, now_lo, now_hi)
    //         if (now > this->last_time + duration) return 0.0f;   // the light went out
    //         return this->light_value;                            // +0x18
    //
    //   Nothing else reads that state, so the thing you see is the mesh the L-Rod's own vertex
    //   modifier (vertexmodifier_lrod_color, registered as "CLRodColor") draws:
    //
    //     Pleione+0x08adef3  lrod_modifier::get_values(character, vertices, vertex_count)
    //         value = get_light_value(...)
    //         for each vertex: vertices[i].glow = value;   // vertices are 0x18 bytes
    //
    //   That gives the mod two knobs, and they do different things:
    //     - "Light size" scales the positions (x/y/z, the first 12 bytes of each vertex) around the
    //       middle of the mesh, so the light expands in every direction. This is the one that
    //       makes the light bigger.
    //     - "Light brightness" multiplies the value that becomes the per-vertex glow, so the light
    //       becomes brighter without changing shape.
    class BiggerLRodLights : public PatchMod {
    public:
        BiggerLRodLights();

        void onPatchUI() override;
        void onConfigLoad(const Config& cfg) override;
        void onConfigSave(Config& cfg) override;

    private:
        bool m_enabled;
        bool m_isAvailable;
        float m_size;
        float m_brightness;
        Patch m_brightnessPatch;
        Patch m_sizePatch;

        void apply();
    };
}
