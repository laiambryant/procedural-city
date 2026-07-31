#include "material/city_material_builder.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/orm_material3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

static Ref<BaseMaterial3D> instantiate_base_material(bool p_orm) {
	if (p_orm) {
		Ref<ORMMaterial3D> m;
		m.instantiate();
		return m;
	}
	Ref<StandardMaterial3D> m;
	m.instantiate();
	return m;
}

static bool has_pixels(const Ref<Image> &p_image) {
	return p_image.is_valid() && !p_image->is_empty();
}

static void apply_normal_map(const Ref<BaseMaterial3D> &p_mat, const Ref<Image> &p_normal, double p_strength) {
	if (!has_pixels(p_normal)) {
		return;
	}
	p_mat->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
	p_mat->set_texture(BaseMaterial3D::TEXTURE_NORMAL, ImageTexture::create_from_image(p_normal));
	p_mat->set_normal_scale(p_strength);
}

static void apply_roughness(const Ref<BaseMaterial3D> &p_mat, const Ref<Image> &p_map, double p_scalar) {
	if (has_pixels(p_map)) {
		p_mat->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, ImageTexture::create_from_image(p_map));
		p_mat->set_roughness_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_RED);
		p_mat->set_roughness(1.0);
	} else {
		p_mat->set_roughness(p_scalar);
	}
}

static void apply_sampling_options(const Ref<BaseMaterial3D> &p_mat, const CityMaterialSpec &p_spec) {
	p_mat->set_uv1_scale(Vector3(p_spec.uv_scale.x, p_spec.uv_scale.y, 1.0));
	p_mat->set_texture_filter(p_spec.filter_nearest ? BaseMaterial3D::TEXTURE_FILTER_NEAREST : BaseMaterial3D::TEXTURE_FILTER_LINEAR);
	p_mat->set_flag(BaseMaterial3D::FLAG_USE_TEXTURE_REPEAT, p_spec.repeat);
}

// The meshers bake AO and per-cell tint into vertex colours; this multiplies
// them into the albedo. Meshes without a COLOR array are unaffected, so it is
// safe to enable for every backend.
static void enable_baked_vertex_colours(const Ref<BaseMaterial3D> &p_mat) {
	p_mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
}

Ref<Material> godot::build_city_material(const CityMaterialSpec &p_spec) {
	if (p_spec.albedo.is_null()) {
		return Ref<Material>();
	}
	Ref<BaseMaterial3D> mat = instantiate_base_material(p_spec.orm);
	mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ImageTexture::create_from_image(p_spec.albedo));
	enable_baked_vertex_colours(mat);
	apply_normal_map(mat, p_spec.normal_map, p_spec.normal_strength);
	apply_roughness(mat, p_spec.roughness_map, p_spec.roughness);
	mat->set_metallic(p_spec.metallic);
	apply_sampling_options(mat, p_spec);
	return mat;
}

Ref<Image> godot::compose_rgb_albedo(const Ref<Image> &p_r, const Ref<Image> &p_g, const Ref<Image> &p_b) {
	if (p_r.is_null() || p_g.is_null() || p_b.is_null()) {
		return Ref<Image>();
	}
	p_r->convert(Image::FORMAT_RGBA8);
	p_g->convert(Image::FORMAT_RGBA8);
	p_b->convert(Image::FORMAT_RGBA8);

	const int w = p_r->get_width();
	const int h = p_r->get_height();
	if (w <= 0 || h <= 0 ||
		p_g->get_width() != w || p_g->get_height() != h ||
		p_b->get_width() != w || p_b->get_height() != h) {
		UtilityFunctions::push_warning("[ProcCity] R/G/B channel maps have mismatched sizes; cannot compose albedo.");
		return Ref<Image>();
	}

	const PackedByteArray rd = p_r->get_data();
	const PackedByteArray gd = p_g->get_data();
	const PackedByteArray bd = p_b->get_data();
	const uint8_t *rp = rd.ptr();
	const uint8_t *gp = gd.ptr();
	const uint8_t *bp = bd.ptr();

	const int64_t n = (int64_t)w * (int64_t)h;
	PackedByteArray out;
	out.resize(n * 3);
	uint8_t *op = out.ptrw();
	for (int64_t i = 0; i < n; i++) {
		op[i * 3 + 0] = rp[i * 4];
		op[i * 3 + 1] = gp[i * 4];
		op[i * 3 + 2] = bp[i * 4];
	}
	return Image::create_from_data(w, h, false, Image::FORMAT_RGB8, out);
}
