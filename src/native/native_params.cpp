#include "native/native_params.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;

static const char *SPRITE_ROOT = "res://addons/procedural_city/sprites/";

static cppdx::DualRange to_range(const Vector2i &p_value) {
	return cppdx::DualRange{ (int64_t)p_value.x, (int64_t)p_value.y };
}

cppdx::Params godot::to_native_params(const Ref<GoplacementxParams> &p_params) {
	cppdx::Params out;
	if (p_params.is_null()) {
		return out;
	}

	out.iterations = p_params->get_iterations();
	out.background_brightness = p_params->get_background_brightness();

	out.rect_enabled = p_params->get_rect_enabled();
	out.rect_brightness = to_range(p_params->get_rect_brightness());
	out.rect_alpha = to_range(p_params->get_rect_alpha());
	out.rect_scale = p_params->get_rect_scale();

	out.grid_enabled = p_params->get_grid_enabled();
	out.grid_brightness = to_range(p_params->get_grid_brightness());
	out.grid_alpha = to_range(p_params->get_grid_alpha());
	out.grid_scale = p_params->get_grid_scale();
	out.grid_amount = to_range(p_params->get_grid_amount());
	out.grid_gap = p_params->get_grid_gap();

	out.cols_enabled = p_params->get_cols_enabled();
	out.cols_brightness = to_range(p_params->get_cols_brightness());
	out.cols_alpha = to_range(p_params->get_cols_alpha());
	out.cols_scale = p_params->get_cols_scale();
	out.cols_amount = to_range(p_params->get_cols_amount());
	out.cols_gap = p_params->get_cols_gap();

	out.rows_enabled = p_params->get_rows_enabled();
	out.rows_brightness = to_range(p_params->get_rows_brightness());
	out.rows_alpha = to_range(p_params->get_rows_alpha());
	out.rows_scale = p_params->get_rows_scale();
	out.rows_amount = to_range(p_params->get_rows_amount());
	out.rows_gap = p_params->get_rows_gap();

	out.lines_enabled = p_params->get_lines_enabled();
	out.lines_brightness = to_range(p_params->get_lines_brightness());
	out.lines_alpha = to_range(p_params->get_lines_alpha());
	out.lines_width = to_range(p_params->get_lines_width());

	out.sprites_enabled = p_params->get_sprites_enabled();
	out.sprite_pack_mask = (uint32_t)p_params->get_sprite_packs();
	out.sprites_rotation_enabled = p_params->get_sprites_rotation_enabled();
	out.seamless = p_params->get_seamless();
	out.composition_mode_mask = (uint32_t)p_params->get_composition_modes();
	return out;
}

Vector2i godot::native_canvas_size(const Ref<GoplacementxParams> &p_params) {
	if (p_params.is_null()) {
		return Vector2i(GPX_DEFAULT_RESOLUTION, GPX_DEFAULT_RESOLUTION);
	}
	const int width = p_params->get_out_width();
	const int height = p_params->get_out_height();
	if (width > 0 && height > 0) {
		return Vector2i(width, height);
	}
	const int resolution = p_params->get_resolution() > 0 ? p_params->get_resolution() : GPX_DEFAULT_RESOLUTION;
	return Vector2i(resolution, resolution);
}

std::vector<cppdx::ColorRgb> godot::to_native_gradient(const Ref<GoplacementxParams> &p_params) {
	if (p_params.is_null()) {
		return cppdx::default_gradient();
	}
	const PackedColorArray colors = p_params->get_effective_gradient();
	std::vector<cppdx::ColorRgb> stops;
	stops.reserve((size_t)colors.size());
	for (int i = 0; i < colors.size(); i++) {
		const Color c = colors[i];
		stops.push_back(cppdx::ColorRgb{
				(uint8_t)CLAMP((int)(c.r * 255.0f + 0.5f), 0, 255),
				(uint8_t)CLAMP((int)(c.g * 255.0f + 0.5f), 0, 255),
				(uint8_t)CLAMP((int)(c.b * 255.0f + 0.5f), 0, 255) });
	}
	return stops;
}

cppdx::OutputMode godot::to_native_output_mode(const String &p_mode) {
	if (p_mode == "normal") {
		return cppdx::OutputMode::NORMAL;
	}
	if (p_mode == "color") {
		return cppdx::OutputMode::COLOR;
	}
	return cppdx::OutputMode::GRAYSCALE;
}

// The imported texture is the only copy an exported build ships: reading the
// .png next to it works while authoring but resolves to nothing once exported.
static Ref<Image> imported_sprite_image(const String &p_path) {
	Ref<Texture2D> texture = ResourceLoader::get_singleton()->load(p_path);
	if (texture.is_null()) {
		return Ref<Image>();
	}
	Ref<Image> image = texture->get_image();
	if (image.is_null()) {
		return Ref<Image>();
	}
	return image->duplicate();
}

static Ref<Image> load_sprite_image(const String &p_path) {
	Ref<Image> imported = imported_sprite_image(p_path);
	if (imported.is_valid() && !imported->is_empty()) {
		return imported;
	}
	return Image::load_from_file(p_path);
}

static bool make_rgba8(const Ref<Image> &p_image) {
	if (p_image->is_compressed() && p_image->decompress() != OK) {
		return false;
	}
	if (p_image->get_format() != Image::FORMAT_RGBA8) {
		p_image->convert(Image::FORMAT_RGBA8);
	}
	return p_image->get_format() == Image::FORMAT_RGBA8;
}

static bool append_sprite(cppdx::SpriteAtlas &r_atlas, const String &p_path) {
	Ref<Image> image = load_sprite_image(p_path);
	if (image.is_null() || image->is_empty()) {
		return false;
	}
	if (image->get_width() != image->get_height()) {
		return false;
	}
	if (!make_rgba8(image)) {
		return false;
	}
	const PackedByteArray data = image->get_data();
	const uint32_t side = (uint32_t)image->get_width();
	std::vector<uint32_t> pixels((size_t)side * (size_t)side, 0u);
	memcpy(pixels.data(), data.ptr(), pixels.size() * sizeof(uint32_t));
	r_atlas.add_sprite(pixels.data(), side);
	return true;
}

cppdx::SpriteAtlas godot::build_native_atlas(const cppdx::Params &p_params) {
	cppdx::SpriteAtlas atlas;
	if (!p_params.sprites_enabled) {
		atlas.make_empty_if_unused();
		return atlas;
	}
	for (uint32_t pack : cppdx::selected_sprite_packs(p_params.sprite_pack_mask)) {
		const String folder = String(SPRITE_ROOT) + cppdx::sprite_pack_name(pack) + "/";
		const uint32_t count = cppdx::sprite_pack_length(pack);
		for (uint32_t i = 1; i <= count; i++) {
			if (!append_sprite(atlas, folder + String::num_uint64(i) + ".png")) {
				UtilityFunctions::push_warning("[ProcCity] Sprite " + folder + String::num_uint64(i) + ".png could not be loaded.");
			}
		}
	}
	atlas.make_empty_if_unused();
	return atlas;
}

Ref<Image> godot::canvas_to_image(const cppdx::Canvas &p_canvas) {
	PackedByteArray bytes;
	bytes.resize((int64_t)p_canvas.byte_size());
	memcpy(bytes.ptrw(), p_canvas.pixels.data(), p_canvas.byte_size());
	return Image::create_from_data((int32_t)p_canvas.width, (int32_t)p_canvas.height, false, Image::FORMAT_RGBA8, bytes);
}
