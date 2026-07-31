#include "cli/goplacementx_params.h"

#include "cli/goplacementx_param_names.h"

#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/variant/color.hpp>

using namespace godot;

// Everything that turns the palette parameters into the gradient string the CLI
// takes on its command line: the built-in presets, the hex conversions, and the
// seeded random palette behind the Randomize Palette button.

struct PalettePreset {
	int count;
	uint32_t colors[5];
};

static const PalettePreset PALETTE_PRESETS[] = {
	{ 0, {} },													 // Custom: use the user-edited gradient_colors
	{ 2, { 0x000000, 0xffffff } },								 // Grayscale
	{ 3, { 0x00ffff, 0x9500ff, 0xffe500 } },					 // DisplacementX (goplacementx default)
	{ 5, { 0x000000, 0x7a0000, 0xff6a00, 0xffe808, 0xffffff } }, // Fire
	{ 4, { 0x001b2e, 0x1b4965, 0x5fa8d3, 0xcae9ff } },			 // Ocean
	{ 5, { 0x2b0a3d, 0x7b2d6b, 0xe85d75, 0xffb86b, 0xffe9a8 } }, // Sunset
	{ 3, { 0xff00ff, 0x00ffff, 0xfaff00 } },					 // Neon
	{ 5, { 0x2a4d1e, 0x6b8e23, 0xc2b280, 0x8b5a2b, 0xffffff } }, // Terrain
	{ 4, { 0x440154, 0x31688e, 0x35b779, 0xfde725 } },			 // Viridis
};

static Color color_from_hex(uint32_t p_hex) {
	return Color(
			(float)((p_hex >> 16) & 0xFF) / 255.0f,
			(float)((p_hex >> 8) & 0xFF) / 255.0f,
			(float)(p_hex & 0xFF) / 255.0f);
}

static PackedColorArray preset_palette(int p_idx) {
	PackedColorArray a;
	if (p_idx < 0 || p_idx >= (int)(sizeof(PALETTE_PRESETS) / sizeof(PALETTE_PRESETS[0]))) {
		return a;
	}
	const PalettePreset &preset = PALETTE_PRESETS[p_idx];
	for (int i = 0; i < preset.count; i++) {
		a.push_back(color_from_hex(preset.colors[i]));
	}
	return a;
}

// Colour channels are floats in [0, 1]; the gradient string the CLI takes wants
// them as 8-bit hex, so each channel scales by this and rounds.
static constexpr float COLOR_BYTE_MAX = 255.0f;

static String byte_to_hex(int p_v) {
	static const char *digits = "0123456789abcdef";
	p_v = CLAMP(p_v, 0, (int)COLOR_BYTE_MAX);
	char buf[3];
	buf[0] = digits[(p_v >> 4) & 0xF];
	buf[1] = digits[p_v & 0xF];
	buf[2] = '\0';
	return String(buf);
}

PackedColorArray GoplacementxParams::get_effective_gradient() const {
	if (palette_preset == GPX_PALETTE_CUSTOM) {
		return gradient_colors;
	}
	return preset_palette(palette_preset);
}

String GoplacementxParams::gradient_to_string() const {
	const PackedColorArray grad = get_effective_gradient();
	String out;
	for (int i = 0; i < grad.size(); i++) {
		const Color c = grad[i];
		if (i > 0) {
			out += ",";
		}
		out += "#";
		out += byte_to_hex((int)(c.r * COLOR_BYTE_MAX + 0.5f));
		out += byte_to_hex((int)(c.g * COLOR_BYTE_MAX + 0.5f));
		out += byte_to_hex((int)(c.b * COLOR_BYTE_MAX + 0.5f));
	}
	return out;
}

void GoplacementxParams::generate_random_palette(int p_stops, int p_seed) {
	if (p_stops < GPX_MIN_PALETTE_STOPS) {
		p_stops = GPX_MIN_PALETTE_STOPS;
	}
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	if (p_seed != 0) {
		rng->set_seed((uint64_t)p_seed);
	} else {
		rng->randomize();
	}

	// The ramp walks hue over a limited span while value rises dark -> light,
	// so the palette reads well as a height-mapped albedo.
	constexpr float HUE_SPAN_MIN = 0.1f;
	constexpr float HUE_SPAN_MAX = 0.6f;
	constexpr float SATURATION_MIN = 0.5f;
	constexpr float SATURATION_MAX = 1.0f;
	constexpr float VALUE_DARKEST = 0.15f;
	constexpr float VALUE_BRIGHTEST = 1.0f;

	const float base_h = rng->randf();
	const float hue_span = rng->randf_range(HUE_SPAN_MIN, HUE_SPAN_MAX);
	const bool ascending = rng->randf() < 0.5f;

	PackedColorArray a;
	for (int i = 0; i < p_stops; i++) {
		const float t = (p_stops > 1) ? (float)i / (float)(p_stops - 1) : 0.0f;
		float h = base_h + (ascending ? t : -t) * hue_span;
		h -= (float)(int)h;
		if (h < 0.0f) {
			h += 1.0f;
		}
		const float s = rng->randf_range(SATURATION_MIN, SATURATION_MAX);
		const float v = VALUE_DARKEST + t * (VALUE_BRIGHTEST - VALUE_DARKEST);
		a.push_back(Color::from_hsv(h, s, v));
	}

	gradient_colors = a;
	palette_preset = GPX_PALETTE_CUSTOM;
	emit_changed();
}

void GoplacementxParams::randomize_palette() {
	generate_random_palette(GPX_RANDOM_PALETTE_STOPS, GPX_FRESH_SEED);
}

Callable GoplacementxParams::_btn_randomize_palette() const {
	return Callable(const_cast<GoplacementxParams *>(this), "randomize_palette");
}
