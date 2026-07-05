#include "goplacementx_params.h"

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>

using namespace godot;

static const char *PALETTE_HINT =
		"Custom,Grayscale,DisplacementX,Fire,Ocean,Sunset,Neon,Terrain,Viridis";

static Color rgb(uint32_t p_hex) {
	return Color(
			(float)((p_hex >> 16) & 0xFF) / 255.0f,
			(float)((p_hex >> 8) & 0xFF) / 255.0f,
			(float)(p_hex & 0xFF) / 255.0f);
}

static PackedColorArray preset_palette(int p_idx) {
	PackedColorArray a;
	switch (p_idx) {
		case 1: // Grayscale
			a.push_back(rgb(0x000000));
			a.push_back(rgb(0xffffff));
			break;
		case 2: // DisplacementX (goplacementx default)
			a.push_back(rgb(0x00ffff));
			a.push_back(rgb(0x9500ff));
			a.push_back(rgb(0xffe500));
			break;
		case 3: // Fire
			a.push_back(rgb(0x000000));
			a.push_back(rgb(0x7a0000));
			a.push_back(rgb(0xff6a00));
			a.push_back(rgb(0xffe808));
			a.push_back(rgb(0xffffff));
			break;
		case 4: // Ocean
			a.push_back(rgb(0x001b2e));
			a.push_back(rgb(0x1b4965));
			a.push_back(rgb(0x5fa8d3));
			a.push_back(rgb(0xcae9ff));
			break;
		case 5: // Sunset
			a.push_back(rgb(0x2b0a3d));
			a.push_back(rgb(0x7b2d6b));
			a.push_back(rgb(0xe85d75));
			a.push_back(rgb(0xffb86b));
			a.push_back(rgb(0xffe9a8));
			break;
		case 6: // Neon
			a.push_back(rgb(0xff00ff));
			a.push_back(rgb(0x00ffff));
			a.push_back(rgb(0xfaff00));
			break;
		case 7: // Terrain
			a.push_back(rgb(0x2a4d1e));
			a.push_back(rgb(0x6b8e23));
			a.push_back(rgb(0xc2b280));
			a.push_back(rgb(0x8b5a2b));
			a.push_back(rgb(0xffffff));
			break;
		case 8: // Viridis
			a.push_back(rgb(0x440154));
			a.push_back(rgb(0x31688e));
			a.push_back(rgb(0x35b779));
			a.push_back(rgb(0xfde725));
			break;
		default: // Custom -> use the user-edited gradient_colors
			break;
	}
	return a;
}

static const char *COMPOSITION_NAMES[16] = {
	"color-burn", "color-dodge", "darken", "difference", "exclusion",
	"hard-light", "lighten", "lighter", "luminosity", "multiply",
	"overlay", "screen", "soft-light", "source-atop", "source-over", "xor"
};

static const char *SPRITE_PACK_NAMES[4] = {
	"classic", "bigdata", "aggromaxx", "crappack"
};

static const char *COMPOSITION_HINT =
		"color-burn,color-dodge,darken,difference,exclusion,hard-light,lighten,"
		"lighter,luminosity,multiply,overlay,screen,soft-light,source-atop,"
		"source-over,xor";

static const char *SPRITE_PACK_HINT = "classic,bigdata,aggromaxx,crappack";

static Array dual_to_array(const Vector2i &p_v) {
	Array a;
	a.push_back(p_v.x);
	a.push_back(p_v.y);
	return a;
}

static Vector2i array_to_dual(const Variant &p_v, const Vector2i &p_fallback) {
	if (p_v.get_type() != Variant::ARRAY) {
		return p_fallback;
	}
	Array a = p_v;
	if (a.size() < 2) {
		return p_fallback;
	}
	return Vector2i((int)a[0], (int)a[1]);
}

static String byte_to_hex(int p_v) {
	static const char *digits = "0123456789abcdef";
	if (p_v < 0) {
		p_v = 0;
	}
	if (p_v > 255) {
		p_v = 255;
	}
	char buf[3];
	buf[0] = digits[(p_v >> 4) & 0xF];
	buf[1] = digits[p_v & 0xF];
	buf[2] = '\0';
	return String(buf);
}

void GoplacementxParams::_bind_methods() {
#define BIND_FULL(m_variant, m_name, m_hint, m_hintstr)                                              \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &GoplacementxParams::set_##m_name);      \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &GoplacementxParams::get_##m_name);               \
	ADD_PROPERTY(PropertyInfo(m_variant, #m_name, m_hint, m_hintstr), "set_" #m_name, "get_" #m_name);
#define BIND_PLAIN(m_variant, m_name) BIND_FULL(m_variant, m_name, PROPERTY_HINT_NONE, "")

	ADD_GROUP("Texture", "");
	BIND_FULL(Variant::INT, resolution, PROPERTY_HINT_RANGE, "16,8192,1");
	BIND_FULL(Variant::INT, out_width, PROPERTY_HINT_RANGE, "0,8192,1");
	BIND_FULL(Variant::INT, out_height, PROPERTY_HINT_RANGE, "0,8192,1");
	BIND_FULL(Variant::INT, palette_preset, PROPERTY_HINT_ENUM, PALETTE_HINT);
	BIND_PLAIN(Variant::PACKED_COLOR_ARRAY, gradient_colors);
	// Randomize Palette tool-button sits directly under the gradient colours.
	ClassDB::bind_method(D_METHOD("randomize_palette"), &GoplacementxParams::randomize_palette);
	ClassDB::bind_method(D_METHOD("_btn_randomize_palette"), &GoplacementxParams::_btn_randomize_palette);
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "randomize_palette_action", PROPERTY_HINT_TOOL_BUTTON, "Randomize Palette", PROPERTY_USAGE_EDITOR), "", "_btn_randomize_palette");
	BIND_PLAIN(Variant::BOOL, invert);
	BIND_PLAIN(Variant::BOOL, seamless);

	ADD_GROUP("Seed", "");
	BIND_PLAIN(Variant::INT, seed);
	BIND_PLAIN(Variant::BOOL, randomize_seed);

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
	for (int i = 0; i < 16; i++) {
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
	for (int i = 0; i < 4; i++) {
		if (sprite_packs & (1 << i)) {
			out.push_back(SPRITE_PACK_NAMES[i]);
		}
	}
	return out;
}

PackedColorArray GoplacementxParams::get_effective_gradient() const {
	if (palette_preset == 0) {
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
	if (p_stops < 2) {
		p_stops = 2;
	}
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	if (p_seed != 0) {
		rng->set_seed((uint64_t)p_seed);
	} else {
		rng->randomize();
	}

	const float base_h = rng->randf();
	const float hue_span = rng->randf_range(0.1f, 0.6f);
	const bool ascending = rng->randf() < 0.5f;

	PackedColorArray a;
	for (int i = 0; i < p_stops; i++) {
		const float t = (p_stops > 1) ? (float)i / (float)(p_stops - 1) : 0.0f;
		float h = base_h + (ascending ? t : -t) * hue_span;
		h -= (float)(int)h;
		if (h < 0.0f) {
			h += 1.0f;
		}
		const float s = rng->randf_range(0.5f, 1.0f);
		const float v = 0.15f + t * 0.85f;
		a.push_back(Color::from_hsv(h, s, v));
	}

	gradient_colors = a;
	palette_preset = 0;
	emit_changed();
}

void GoplacementxParams::randomize_palette() {
	generate_random_palette(4, 0);
}

Callable GoplacementxParams::_btn_randomize_palette() const {
	return Callable(const_cast<GoplacementxParams *>(this), "randomize_palette");
}

Dictionary GoplacementxParams::to_dict() const {
	Dictionary d;
	d["iterations"] = iterations;
	d["backgroundBrightness"] = background_brightness;

	d["rectEnabled"] = rect_enabled;
	d["rectBrightness"] = dual_to_array(rect_brightness);
	d["rectAlpha"] = dual_to_array(rect_alpha);
	d["rectScale"] = rect_scale;

	d["gridEnabled"] = grid_enabled;
	d["gridBrightness"] = dual_to_array(grid_brightness);
	d["gridAlpha"] = dual_to_array(grid_alpha);
	d["gridScale"] = grid_scale;
	d["gridAmount"] = dual_to_array(grid_amount);
	d["gridGap"] = grid_gap;

	d["colsEnabled"] = cols_enabled;
	d["colsBrightness"] = dual_to_array(cols_brightness);
	d["colsAlpha"] = dual_to_array(cols_alpha);
	d["colsScale"] = cols_scale;
	d["colsAmount"] = dual_to_array(cols_amount);
	d["colsGap"] = cols_gap;

	d["rowsEnabled"] = rows_enabled;
	d["rowsBrightness"] = dual_to_array(rows_brightness);
	d["rowsAlpha"] = dual_to_array(rows_alpha);
	d["rowsScale"] = rows_scale;
	d["rowsAmount"] = dual_to_array(rows_amount);
	d["rowsGap"] = rows_gap;

	d["linesEnabled"] = lines_enabled;
	d["linesBrightness"] = dual_to_array(lines_brightness);
	d["linesAlpha"] = dual_to_array(lines_alpha);
	d["linesWidth"] = dual_to_array(lines_width);

	d["spritesEnabled"] = sprites_enabled;
	Array packs;
	const PackedStringArray pack_list = sprite_packs_list();
	for (int i = 0; i < pack_list.size(); i++) {
		packs.push_back(pack_list[i]);
	}
	d["spritesPacks"] = packs;
	d["spritesRotationEnabled"] = sprites_rotation_enabled;
	d["seamlessTextureEnabled"] = seamless;

	Array modes;
	const PackedStringArray mode_list = composition_modes_list();
	for (int i = 0; i < mode_list.size(); i++) {
		modes.push_back(mode_list[i]);
	}
	d["compositionModes"] = modes;

	return d;
}

String GoplacementxParams::to_json() const {
	return JSON::stringify(to_dict(), "\t", false);
}

void GoplacementxParams::from_dict(const Dictionary &p_dict) {
	if (p_dict.has("iterations")) {
		iterations = (int)p_dict["iterations"];
	}
	if (p_dict.has("backgroundBrightness")) {
		background_brightness = (int)p_dict["backgroundBrightness"];
	}

	if (p_dict.has("rectEnabled")) {
		rect_enabled = (bool)p_dict["rectEnabled"];
	}
	rect_brightness = array_to_dual(p_dict.get("rectBrightness", Variant()), rect_brightness);
	rect_alpha = array_to_dual(p_dict.get("rectAlpha", Variant()), rect_alpha);
	if (p_dict.has("rectScale")) {
		rect_scale = (int)p_dict["rectScale"];
	}

	if (p_dict.has("gridEnabled")) {
		grid_enabled = (bool)p_dict["gridEnabled"];
	}
	grid_brightness = array_to_dual(p_dict.get("gridBrightness", Variant()), grid_brightness);
	grid_alpha = array_to_dual(p_dict.get("gridAlpha", Variant()), grid_alpha);
	if (p_dict.has("gridScale")) {
		grid_scale = (int)p_dict["gridScale"];
	}
	grid_amount = array_to_dual(p_dict.get("gridAmount", Variant()), grid_amount);
	if (p_dict.has("gridGap")) {
		grid_gap = (int)p_dict["gridGap"];
	}

	if (p_dict.has("colsEnabled")) {
		cols_enabled = (bool)p_dict["colsEnabled"];
	}
	cols_brightness = array_to_dual(p_dict.get("colsBrightness", Variant()), cols_brightness);
	cols_alpha = array_to_dual(p_dict.get("colsAlpha", Variant()), cols_alpha);
	if (p_dict.has("colsScale")) {
		cols_scale = (int)p_dict["colsScale"];
	}
	cols_amount = array_to_dual(p_dict.get("colsAmount", Variant()), cols_amount);
	if (p_dict.has("colsGap")) {
		cols_gap = (int)p_dict["colsGap"];
	}

	if (p_dict.has("rowsEnabled")) {
		rows_enabled = (bool)p_dict["rowsEnabled"];
	}
	rows_brightness = array_to_dual(p_dict.get("rowsBrightness", Variant()), rows_brightness);
	rows_alpha = array_to_dual(p_dict.get("rowsAlpha", Variant()), rows_alpha);
	if (p_dict.has("rowsScale")) {
		rows_scale = (int)p_dict["rowsScale"];
	}
	rows_amount = array_to_dual(p_dict.get("rowsAmount", Variant()), rows_amount);
	if (p_dict.has("rowsGap")) {
		rows_gap = (int)p_dict["rowsGap"];
	}

	if (p_dict.has("linesEnabled")) {
		lines_enabled = (bool)p_dict["linesEnabled"];
	}
	lines_brightness = array_to_dual(p_dict.get("linesBrightness", Variant()), lines_brightness);
	lines_alpha = array_to_dual(p_dict.get("linesAlpha", Variant()), lines_alpha);
	lines_width = array_to_dual(p_dict.get("linesWidth", Variant()), lines_width);

	if (p_dict.has("spritesEnabled")) {
		sprites_enabled = (bool)p_dict["spritesEnabled"];
	}
	if (p_dict.has("spritesPacks")) {
		Array packs = p_dict["spritesPacks"];
		sprite_packs = 0;
		for (int i = 0; i < packs.size(); i++) {
			const String name = packs[i];
			for (int b = 0; b < 4; b++) {
				if (name == SPRITE_PACK_NAMES[b]) {
					sprite_packs |= (1 << b);
				}
			}
		}
	}
	if (p_dict.has("spritesRotationEnabled")) {
		sprites_rotation_enabled = (bool)p_dict["spritesRotationEnabled"];
	}
	if (p_dict.has("seamlessTextureEnabled")) {
		seamless = (bool)p_dict["seamlessTextureEnabled"];
	}
	if (p_dict.has("compositionModes")) {
		Array modes = p_dict["compositionModes"];
		composition_modes = 0;
		for (int i = 0; i < modes.size(); i++) {
			const String name = modes[i];
			for (int b = 0; b < 16; b++) {
				if (name == COMPOSITION_NAMES[b]) {
					composition_modes |= (1 << b);
				}
			}
		}
	}

	emit_changed();
}
