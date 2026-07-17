#include "cli/goplacementx_params.h"

#include "cli/goplacementx_param_names.h"

#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>

using namespace godot;

static const char *PALETTE_HINT =
		"Custom,Grayscale,DisplacementX,Fire,Ocean,Sunset,Neon,Terrain,Viridis";

static const char *COMPOSITION_HINT =
		"color-burn,color-dodge,darken,difference,exclusion,hard-light,lighten,"
		"lighter,luminosity,multiply,overlay,screen,soft-light,source-atop,"
		"source-over,xor";

static const char *SPRITE_PACK_HINT = "classic,bigdata,aggromaxx,crappack";

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

static String byte_to_hex(int p_v) {
	static const char *digits = "0123456789abcdef";
	p_v = CLAMP(p_v, 0, 255);
	char buf[3];
	buf[0] = digits[(p_v >> 4) & 0xF];
	buf[1] = digits[p_v & 0xF];
	buf[2] = '\0';
	return String(buf);
}

void GoplacementxParams::_bind_methods() {
#define BIND_FULL(m_variant, m_name, m_hint, m_hintstr)                                         \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &GoplacementxParams::set_##m_name); \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &GoplacementxParams::get_##m_name);          \
	ADD_PROPERTY(PropertyInfo(m_variant, #m_name, m_hint, m_hintstr), "set_" #m_name, "get_" #m_name);
#define BIND_PLAIN(m_variant, m_name) BIND_FULL(m_variant, m_name, PROPERTY_HINT_NONE, "")

	ADD_GROUP("Texture", "");
	BIND_FULL(Variant::INT, resolution, PROPERTY_HINT_RANGE, "16,8192,1");
	BIND_FULL(Variant::INT, out_width, PROPERTY_HINT_RANGE, "0,8192,1");
	BIND_FULL(Variant::INT, out_height, PROPERTY_HINT_RANGE, "0,8192,1");
	BIND_FULL(Variant::INT, palette_preset, PROPERTY_HINT_ENUM, PALETTE_HINT);
	BIND_PLAIN(Variant::PACKED_COLOR_ARRAY, gradient_colors);
	ClassDB::bind_method(D_METHOD("randomize_palette"), &GoplacementxParams::randomize_palette);
	ClassDB::bind_method(D_METHOD("_btn_randomize_palette"), &GoplacementxParams::_btn_randomize_palette);
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "randomize_palette_action", PROPERTY_HINT_TOOL_BUTTON, "Randomize Palette", PROPERTY_USAGE_EDITOR), "", "_btn_randomize_palette");
	BIND_PLAIN(Variant::BOOL, invert);
	BIND_PLAIN(Variant::BOOL, seamless);

	ADD_GROUP("Seed", "");
	BIND_PLAIN(Variant::INT, seed);
	BIND_PLAIN(Variant::BOOL, randomize_seed);
	BIND_PLAIN(Variant::BOOL, fast);

	ADD_GROUP("Generator", "");
	BIND_FULL(Variant::INT, iterations, PROPERTY_HINT_RANGE, "10,2000,1");
	BIND_FULL(Variant::INT, background_brightness, PROPERTY_HINT_RANGE, "0,255,1");

	ADD_GROUP("Rect Layer", "rect_");
	BIND_PLAIN(Variant::BOOL, rect_enabled);
	BIND_PLAIN(Variant::VECTOR2I, rect_brightness);
	BIND_PLAIN(Variant::VECTOR2I, rect_alpha);
	BIND_FULL(Variant::INT, rect_scale, PROPERTY_HINT_RANGE, "20,200,1");

	ADD_GROUP("Grid Layer", "grid_");
	BIND_PLAIN(Variant::BOOL, grid_enabled);
	BIND_PLAIN(Variant::VECTOR2I, grid_brightness);
	BIND_PLAIN(Variant::VECTOR2I, grid_alpha);
	BIND_FULL(Variant::INT, grid_scale, PROPERTY_HINT_RANGE, "20,200,1");
	BIND_PLAIN(Variant::VECTOR2I, grid_amount);
	BIND_FULL(Variant::INT, grid_gap, PROPERTY_HINT_RANGE, "10,1000,10");

	ADD_GROUP("Cols Layer", "cols_");
	BIND_PLAIN(Variant::BOOL, cols_enabled);
	BIND_PLAIN(Variant::VECTOR2I, cols_brightness);
	BIND_PLAIN(Variant::VECTOR2I, cols_alpha);
	BIND_FULL(Variant::INT, cols_scale, PROPERTY_HINT_RANGE, "20,200,1");
	BIND_PLAIN(Variant::VECTOR2I, cols_amount);
	BIND_FULL(Variant::INT, cols_gap, PROPERTY_HINT_RANGE, "10,1000,10");

	ADD_GROUP("Rows Layer", "rows_");
	BIND_PLAIN(Variant::BOOL, rows_enabled);
	BIND_PLAIN(Variant::VECTOR2I, rows_brightness);
	BIND_PLAIN(Variant::VECTOR2I, rows_alpha);
	BIND_FULL(Variant::INT, rows_scale, PROPERTY_HINT_RANGE, "20,200,1");
	BIND_PLAIN(Variant::VECTOR2I, rows_amount);
	BIND_FULL(Variant::INT, rows_gap, PROPERTY_HINT_RANGE, "10,1000,10");

	ADD_GROUP("Lines Layer", "lines_");
	BIND_PLAIN(Variant::BOOL, lines_enabled);
	BIND_PLAIN(Variant::VECTOR2I, lines_brightness);
	BIND_PLAIN(Variant::VECTOR2I, lines_alpha);
	BIND_PLAIN(Variant::VECTOR2I, lines_width);

	ADD_GROUP("Sprites", "");
	BIND_PLAIN(Variant::BOOL, sprites_enabled);
	BIND_FULL(Variant::INT, sprite_packs, PROPERTY_HINT_FLAGS, SPRITE_PACK_HINT);
	BIND_PLAIN(Variant::BOOL, sprites_rotation_enabled);
	BIND_FULL(Variant::INT, composition_modes, PROPERTY_HINT_FLAGS, COMPOSITION_HINT);

	ClassDB::bind_method(D_METHOD("to_dict"), &GoplacementxParams::to_dict);
	ClassDB::bind_method(D_METHOD("to_json"), &GoplacementxParams::to_json);
	ClassDB::bind_method(D_METHOD("from_dict", "dict"), &GoplacementxParams::from_dict);
	ClassDB::bind_method(D_METHOD("gradient_to_string"), &GoplacementxParams::gradient_to_string);
	ClassDB::bind_method(D_METHOD("get_effective_gradient"), &GoplacementxParams::get_effective_gradient);
	ClassDB::bind_method(D_METHOD("generate_random_palette", "stops", "seed"), &GoplacementxParams::generate_random_palette);
	ClassDB::bind_method(D_METHOD("composition_modes_list"), &GoplacementxParams::composition_modes_list);
	ClassDB::bind_method(D_METHOD("sprite_packs_list"), &GoplacementxParams::sprite_packs_list);

#undef BIND_FULL
#undef BIND_PLAIN
}

PackedStringArray GoplacementxParams::composition_modes_list() const {
	PackedStringArray out;
	for (int i = 0; i < COMPOSITION_MODE_COUNT; i++) {
		if (composition_modes & (1 << i)) {
			out.push_back(COMPOSITION_NAMES[i]);
		}
	}
	if (out.is_empty()) {
		out.push_back("source-over");
	}
	return out;
}

PackedStringArray GoplacementxParams::sprite_packs_list() const {
	PackedStringArray out;
	for (int i = 0; i < SPRITE_PACK_COUNT; i++) {
		if (sprite_packs & (1 << i)) {
			out.push_back(SPRITE_PACK_NAMES[i]);
		}
	}
	return out;
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
		out += byte_to_hex((int)(c.r * 255.0f + 0.5f));
		out += byte_to_hex((int)(c.g * 255.0f + 0.5f));
		out += byte_to_hex((int)(c.b * 255.0f + 0.5f));
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
