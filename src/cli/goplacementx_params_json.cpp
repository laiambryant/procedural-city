#include "cli/goplacementx_params.h"

#include "cli/goplacementx_param_names.h"

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/array.hpp>

using namespace godot;

// The JSON shape mirrors the goplacementx Params struct: camelCase keys, dual
// [min,max] ranges as two-element arrays, sprite packs and composition modes
// as name lists.

static Array dual_to_array(const Vector2i &p_v) {
	Array a;
	a.push_back(p_v.x);
	a.push_back(p_v.y);
	return a;
}

static void read_dual(const Dictionary &p_dict, const char *p_key, Vector2i &r_value) {
	if (!p_dict.has(p_key)) {
		return;
	}
	const Variant v = p_dict[p_key];
	if (v.get_type() != Variant::ARRAY) {
		return;
	}
	Array a = v;
	if (a.size() < 2) {
		return;
	}
	r_value = Vector2i((int)a[0], (int)a[1]);
}

static void read_int(const Dictionary &p_dict, const char *p_key, int &r_value) {
	if (p_dict.has(p_key)) {
		r_value = (int)p_dict[p_key];
	}
}

static void read_bool(const Dictionary &p_dict, const char *p_key, bool &r_value) {
	if (p_dict.has(p_key)) {
		r_value = (bool)p_dict[p_key];
	}
}

static Array names_from_mask(int p_mask, const char *const *p_names, int p_count) {
	Array out;
	for (int i = 0; i < p_count; i++) {
		if (p_mask & (1 << i)) {
			out.push_back(String(p_names[i]));
		}
	}
	return out;
}

static void read_mask(const Dictionary &p_dict, const char *p_key,
					  const char *const *p_names, int p_count, int &r_mask) {
	if (!p_dict.has(p_key)) {
		return;
	}
	const Array names = p_dict[p_key];
	r_mask = 0;
	for (int i = 0; i < names.size(); i++) {
		const String name = names[i];
		for (int b = 0; b < p_count; b++) {
			if (name == p_names[b]) {
				r_mask |= (1 << b);
			}
		}
	}
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
	d["spritesPacks"] = names_from_mask(sprite_packs, SPRITE_PACK_NAMES, SPRITE_PACK_COUNT);
	d["spritesRotationEnabled"] = sprites_rotation_enabled;
	d["seamlessTextureEnabled"] = seamless;
	d["compositionModes"] = names_from_mask(composition_modes, COMPOSITION_NAMES, COMPOSITION_MODE_COUNT);

	return d;
}

String GoplacementxParams::to_json() const {
	return JSON::stringify(to_dict(), "\t", false);
}

void GoplacementxParams::from_dict(const Dictionary &p_dict) {
	read_int(p_dict, "iterations", iterations);
	read_int(p_dict, "backgroundBrightness", background_brightness);

	read_bool(p_dict, "rectEnabled", rect_enabled);
	read_dual(p_dict, "rectBrightness", rect_brightness);
	read_dual(p_dict, "rectAlpha", rect_alpha);
	read_int(p_dict, "rectScale", rect_scale);

	read_bool(p_dict, "gridEnabled", grid_enabled);
	read_dual(p_dict, "gridBrightness", grid_brightness);
	read_dual(p_dict, "gridAlpha", grid_alpha);
	read_int(p_dict, "gridScale", grid_scale);
	read_dual(p_dict, "gridAmount", grid_amount);
	read_int(p_dict, "gridGap", grid_gap);

	read_bool(p_dict, "colsEnabled", cols_enabled);
	read_dual(p_dict, "colsBrightness", cols_brightness);
	read_dual(p_dict, "colsAlpha", cols_alpha);
	read_int(p_dict, "colsScale", cols_scale);
	read_dual(p_dict, "colsAmount", cols_amount);
	read_int(p_dict, "colsGap", cols_gap);

	read_bool(p_dict, "rowsEnabled", rows_enabled);
	read_dual(p_dict, "rowsBrightness", rows_brightness);
	read_dual(p_dict, "rowsAlpha", rows_alpha);
	read_int(p_dict, "rowsScale", rows_scale);
	read_dual(p_dict, "rowsAmount", rows_amount);
	read_int(p_dict, "rowsGap", rows_gap);

	read_bool(p_dict, "linesEnabled", lines_enabled);
	read_dual(p_dict, "linesBrightness", lines_brightness);
	read_dual(p_dict, "linesAlpha", lines_alpha);
	read_dual(p_dict, "linesWidth", lines_width);

	read_bool(p_dict, "spritesEnabled", sprites_enabled);
	read_mask(p_dict, "spritesPacks", SPRITE_PACK_NAMES, SPRITE_PACK_COUNT, sprite_packs);
	read_bool(p_dict, "spritesRotationEnabled", sprites_rotation_enabled);
	read_bool(p_dict, "seamlessTextureEnabled", seamless);
	read_mask(p_dict, "compositionModes", COMPOSITION_NAMES, COMPOSITION_MODE_COUNT, composition_modes);

	emit_changed();
}
