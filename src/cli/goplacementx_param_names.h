#ifndef GOPLACEMENTX_PARAM_NAMES_H
#define GOPLACEMENTX_PARAM_NAMES_H

namespace godot {

// Bit order of the composition_modes / sprite_packs flags, matching the
// goplacementx CLI's canonical names.

inline constexpr int COMPOSITION_MODE_COUNT = 16;
inline constexpr const char *COMPOSITION_NAMES[COMPOSITION_MODE_COUNT] = {
	"color-burn", "color-dodge", "darken", "difference", "exclusion",
	"hard-light", "lighten", "lighter", "luminosity", "multiply",
	"overlay", "screen", "soft-light", "source-atop", "source-over", "xor"
};

inline constexpr int SPRITE_PACK_COUNT = 4;
inline constexpr const char *SPRITE_PACK_NAMES[SPRITE_PACK_COUNT] = {
	"classic", "bigdata", "aggromaxx", "crappack"
};

} // namespace godot

#endif // GOPLACEMENTX_PARAM_NAMES_H
