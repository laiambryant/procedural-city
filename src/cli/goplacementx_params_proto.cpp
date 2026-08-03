#include "cli/goplacementx_params_proto.h"

#include "cli/goplacementx_param_names.h"

#include <godot_cpp/variant/array.hpp>

using namespace godot;

static void set_dual(displacement::v1::Dual *r_dual, const Vector2i &p_v) {
	r_dual->set_min(p_v.x);
	r_dual->set_max(p_v.y);
}

static Array dual_to_array(const displacement::v1::Dual &p_dual) {
	Array a;
	a.push_back((int64_t)p_dual.min());
	a.push_back((int64_t)p_dual.max());
	return a;
}

static Array names_to_array(const google::protobuf::RepeatedPtrField<std::string> &p_names) {
	Array a;
	for (const std::string &name : p_names) {
		a.push_back(String(name.c_str()));
	}
	return a;
}

static void add_names_from_mask(google::protobuf::RepeatedPtrField<std::string> *r_out,
		int p_mask, const char *const *p_names, int p_count) {
	for (int i = 0; i < p_count; i++) {
		if (p_mask & (1 << i)) {
			r_out->Add(std::string(p_names[i]));
		}
	}
}

displacement::v1::Params godot::params_to_proto(const Ref<GoplacementxParams> &p_params) {
	displacement::v1::Params out;
	if (p_params.is_null()) {
		return out;
	}

	out.set_iterations(p_params->get_iterations());
	out.set_background_brightness(p_params->get_background_brightness());

	out.set_rect_enabled(p_params->get_rect_enabled());
	set_dual(out.mutable_rect_brightness(), p_params->get_rect_brightness());
	set_dual(out.mutable_rect_alpha(), p_params->get_rect_alpha());
	out.set_rect_scale(p_params->get_rect_scale());

	out.set_grid_enabled(p_params->get_grid_enabled());
	set_dual(out.mutable_grid_brightness(), p_params->get_grid_brightness());
	set_dual(out.mutable_grid_alpha(), p_params->get_grid_alpha());
	out.set_grid_scale(p_params->get_grid_scale());
	set_dual(out.mutable_grid_amount(), p_params->get_grid_amount());
	out.set_grid_gap(p_params->get_grid_gap());

	out.set_cols_enabled(p_params->get_cols_enabled());
	set_dual(out.mutable_cols_brightness(), p_params->get_cols_brightness());
	set_dual(out.mutable_cols_alpha(), p_params->get_cols_alpha());
	out.set_cols_scale(p_params->get_cols_scale());
	set_dual(out.mutable_cols_amount(), p_params->get_cols_amount());
	out.set_cols_gap(p_params->get_cols_gap());

	out.set_rows_enabled(p_params->get_rows_enabled());
	set_dual(out.mutable_rows_brightness(), p_params->get_rows_brightness());
	set_dual(out.mutable_rows_alpha(), p_params->get_rows_alpha());
	out.set_rows_scale(p_params->get_rows_scale());
	set_dual(out.mutable_rows_amount(), p_params->get_rows_amount());
	out.set_rows_gap(p_params->get_rows_gap());

	out.set_lines_enabled(p_params->get_lines_enabled());
	set_dual(out.mutable_lines_brightness(), p_params->get_lines_brightness());
	set_dual(out.mutable_lines_alpha(), p_params->get_lines_alpha());
	set_dual(out.mutable_lines_width(), p_params->get_lines_width());

	out.set_sprites_enabled(p_params->get_sprites_enabled());
	add_names_from_mask(out.mutable_sprites_packs(), p_params->get_sprite_packs(), SPRITE_PACK_NAMES, SPRITE_PACK_COUNT);
	out.set_sprites_rotation_enabled(p_params->get_sprites_rotation_enabled());
	out.set_seamless_texture_enabled(p_params->get_seamless());
	add_names_from_mask(out.mutable_composition_modes(), p_params->get_composition_modes(), COMPOSITION_NAMES, COMPOSITION_MODE_COUNT);

	return out;
}

displacement::v1::RenderOptions godot::render_options_from(const Ref<GoplacementxParams> &p_params) {
	displacement::v1::RenderOptions out;
	if (p_params.is_null()) {
		out.set_resolution(GPX_DEFAULT_RESOLUTION);
		return out;
	}

	const int width = p_params->get_out_width();
	const int height = p_params->get_out_height();
	if (width > 0 && height > 0) {
		out.set_width(width);
		out.set_height(height);
	} else {
		out.set_resolution(p_params->get_resolution());
	}
	out.set_invert(p_params->get_invert());
	out.set_fast(p_params->get_fast());
	out.set_gradient(std::string(p_params->gradient_to_string().utf8().get_data()));
	return out;
}

Dictionary godot::proto_params_to_dict(const displacement::v1::Params &p_params) {
	Dictionary d;
	d["iterations"] = (int64_t)p_params.iterations();
	d["backgroundBrightness"] = (int64_t)p_params.background_brightness();

	d["rectEnabled"] = p_params.rect_enabled();
	d["rectBrightness"] = dual_to_array(p_params.rect_brightness());
	d["rectAlpha"] = dual_to_array(p_params.rect_alpha());
	d["rectScale"] = (int64_t)p_params.rect_scale();

	d["gridEnabled"] = p_params.grid_enabled();
	d["gridBrightness"] = dual_to_array(p_params.grid_brightness());
	d["gridAlpha"] = dual_to_array(p_params.grid_alpha());
	d["gridScale"] = (int64_t)p_params.grid_scale();
	d["gridAmount"] = dual_to_array(p_params.grid_amount());
	d["gridGap"] = (int64_t)p_params.grid_gap();

	d["colsEnabled"] = p_params.cols_enabled();
	d["colsBrightness"] = dual_to_array(p_params.cols_brightness());
	d["colsAlpha"] = dual_to_array(p_params.cols_alpha());
	d["colsScale"] = (int64_t)p_params.cols_scale();
	d["colsAmount"] = dual_to_array(p_params.cols_amount());
	d["colsGap"] = (int64_t)p_params.cols_gap();

	d["rowsEnabled"] = p_params.rows_enabled();
	d["rowsBrightness"] = dual_to_array(p_params.rows_brightness());
	d["rowsAlpha"] = dual_to_array(p_params.rows_alpha());
	d["rowsScale"] = (int64_t)p_params.rows_scale();
	d["rowsAmount"] = dual_to_array(p_params.rows_amount());
	d["rowsGap"] = (int64_t)p_params.rows_gap();

	d["linesEnabled"] = p_params.lines_enabled();
	d["linesBrightness"] = dual_to_array(p_params.lines_brightness());
	d["linesAlpha"] = dual_to_array(p_params.lines_alpha());
	d["linesWidth"] = dual_to_array(p_params.lines_width());

	d["spritesEnabled"] = p_params.sprites_enabled();
	d["spritesPacks"] = names_to_array(p_params.sprites_packs());
	d["spritesRotationEnabled"] = p_params.sprites_rotation_enabled();
	d["seamlessTextureEnabled"] = p_params.seamless_texture_enabled();
	d["compositionModes"] = names_to_array(p_params.composition_modes());

	return d;
}
