#ifndef BAKED_STYLE_H
#define BAKED_STYLE_H

#include <cstdint>

namespace godot {

inline constexpr float VARIATION_TINT_SPAN = 0.6f;
inline constexpr float ROOF_AO_SPAN = 0.45f;
inline constexpr float WALL_AO_SPAN = 0.55f;
inline constexpr float FLOOR_AO_SPAN = 0.65f;
inline constexpr float MIN_AO_HEIGHT_REFERENCE = 0.001f;

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
