#include "cli/goplacementx_params.h"

#include "cli/goplacementx_param_names.h"

#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/variant/color.hpp>

using namespace godot;

struct PalettePreset {
	const char *name;
	int count;
	uint32_t colors[5];
};

static const PalettePreset PALETTE_PRESETS[] = {
	{ "Custom", 0, {} },
	{ "Grayscale", 2, { 0x000000, 0xffffff } },
	{ "DisplacementX", 3, { 0x00ffff, 0x9500ff, 0xffe500 } },
	{ "Fire", 5, { 0x000000, 0x7a0000, 0xff6a00, 0xffe808, 0xffffff } },
	{ "Ocean", 4, { 0x001b2e, 0x1b4965, 0x5fa8d3, 0xcae9ff } },
	{ "Sunset", 5, { 0x2b0a3d, 0x7b2d6b, 0xe85d75, 0xffb86b, 0xffe9a8 } },
	{ "Neon", 3, { 0xff00ff, 0x00ffff, 0xfaff00 } },
	{ "Terrain", 5, { 0x2a4d1e, 0x6b8e23, 0xc2b280, 0x8b5a2b, 0xffffff } },
	{ "Viridis", 4, { 0x440154, 0x31688e, 0x35b779, 0xfde725 } },
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

constexpr float HUE_SPAN_MIN = 0.1f;
constexpr float HUE_SPAN_MAX = 0.6f;
constexpr float SATURATION_MIN = 0.5f;
constexpr float SATURATION_MAX = 1.0f;
constexpr float VALUE_DARKEST = 0.15f;
constexpr float VALUE_BRIGHTEST = 1.0f;

struct HeightAlbedoRamp {
	float base_hue;
	float hue_span;
	bool ascending;
};

static HeightAlbedoRamp make_height_albedo_ramp(const Ref<RandomNumberGenerator> &p_rng) {
	HeightAlbedoRamp ramp;
	ramp.base_hue = p_rng->randf();
	ramp.hue_span = p_rng->randf_range(HUE_SPAN_MIN, HUE_SPAN_MAX);
	ramp.ascending = p_rng->randf() < 0.5f;
	return ramp;
}

static Color height_albedo_ramp_color(const HeightAlbedoRamp &p_ramp, float p_t, const Ref<RandomNumberGenerator> &p_rng) {
	float h = p_ramp.base_hue + (p_ramp.ascending ? p_t : -p_t) * p_ramp.hue_span;
	h -= (float)(int)h;
	if (h < 0.0f) {
		h += 1.0f;
	}
	const float s = p_rng->randf_range(SATURATION_MIN, SATURATION_MAX);
	const float v = VALUE_DARKEST + p_t * (VALUE_BRIGHTEST - VALUE_DARKEST);
	return Color::from_hsv(h, s, v);
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

	const HeightAlbedoRamp ramp = make_height_albedo_ramp(rng);

	PackedColorArray a;
	for (int i = 0; i < p_stops; i++) {
		const float t = (p_stops > 1) ? (float)i / (float)(p_stops - 1) : 0.0f;
		a.push_back(height_albedo_ramp_color(ramp, t, rng));
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
