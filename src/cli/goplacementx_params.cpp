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

static bool is_layer_toggle(const String &p_name) {
	return p_name == "rect_enabled" || p_name == "grid_enabled" || p_name == "cols_enabled" ||
			p_name == "rows_enabled" || p_name == "lines_enabled" || p_name == "sprites_enabled";
}

bool GoplacementxParams::property_is_usable(const String &p_name) const {
	if (is_layer_toggle(p_name)) {
		return true;
	}
	if (p_name == "resolution") {
		return !has_explicit_output_size();
	}
	if (p_name == "seed") {
		return !randomize_seed;
	}
	if (p_name == "gradient_colors") {
		return palette_preset == GPX_PALETTE_CUSTOM;
	}
	if (p_name == "sprite_packs" || p_name == "sprites_rotation_enabled") {
		return sprites_enabled;
	}
	if (p_name.begins_with("rect_")) {
		return rect_enabled;
	}
	if (p_name.begins_with("grid_")) {
		return grid_enabled;
	}
	if (p_name.begins_with("cols_")) {
		return cols_enabled;
	}
	if (p_name.begins_with("rows_")) {
		return rows_enabled;
	}
	if (p_name.begins_with("lines_")) {
		return lines_enabled;
	}
	return true;
}

void GoplacementxParams::_validate_property(PropertyInfo &p_property) const {
	if (!property_is_usable(String(p_property.name))) {
		p_property.usage |= PROPERTY_USAGE_READ_ONLY;
	}
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
