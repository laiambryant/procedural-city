#ifndef BAKED_STYLE_H
#define BAKED_STYLE_H

#include <cstdint>

namespace godot {

// Tuning for the baked look shared by the mesh backends: how strongly the
// vertex-colour ambient occlusion and the seeded per-cell tint variation read
// at strength 1.0. The inspector exposes ao_strength / color_variation in
// [0, 1]; these spans set what "1" means.

// Fraction of luminance a cell may lose to its seeded tint at variation = 1.
inline constexpr float VARIATION_TINT_SPAN = 0.6f;
// Maximum darkening of a roof corner fully occluded by taller neighbours.
inline constexpr float ROOF_AO_SPAN = 0.45f;
// Maximum darkening of a wall base at the bottom of a full-depth canyon.
inline constexpr float WALL_AO_SPAN = 0.55f;
// Darkening of the ground plane between hex cells (reads as alley floor).
inline constexpr float FLOOR_AO_SPAN = 0.65f;
// Lower bound for the height that normalizes AO depth, so a zero height
// scale cannot divide by zero.
inline constexpr float MIN_AO_HEIGHT_REFERENCE = 0.001f;

// Salts XORed into the generation seed so every seeded effect draws from an
// independent hash stream. Arbitrary but frozen: changing any of them changes
// every city generated from an existing seed.
inline constexpr uint32_t SALT_CELL_TINT = 0xA511E9B3u;
inline constexpr uint32_t SALT_WARP_X = 0x51ED270Bu;
inline constexpr uint32_t SALT_WARP_Z = 0x9E3779B9u;
inline constexpr uint32_t SALT_JITTER_X = 0x2545F491u;
inline constexpr uint32_t SALT_JITTER_Z = 0x6C8E9CF5u;
inline constexpr uint32_t SALT_RIM_HEIGHT = 0x7F4A7C15u;
inline constexpr uint32_t SALT_HEIGHT_JITTER = 0xB5297A4Du;
inline constexpr uint32_t SALT_CELL_SCALE = 0x68E31DA4u;
inline constexpr uint32_t SALT_CELL_ROTATION = 0x1B56C4E9u;

} // namespace godot

#endif // BAKED_STYLE_H
