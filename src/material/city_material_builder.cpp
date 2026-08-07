#include "material/city_material_builder.h"

#include "meshing/parallel_rows.h"

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
	BaseMaterial3D::TextureFilter filter = BaseMaterial3D::TEXTURE_FILTER_LINEAR;
	if (p_spec.texture_filter == 0) {
		filter = BaseMaterial3D::TEXTURE_FILTER_NEAREST;
	} else if (p_spec.texture_filter == 2) {
		filter = BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC;
	}
	p_mat->set_texture_filter(filter);
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

void godot::prepare_material_image(const Ref<Image> &p_image, int p_max_size, bool p_mipmaps, bool p_normal_map) {
	if (!has_pixels(p_image)) {
		return;
	}
	const int width = p_image->get_width();
	const int height = p_image->get_height();
	const int longest = MAX(width, height);
	if (p_max_size > 0 && longest > p_max_size) {
		const double scale = (double)p_max_size / (double)longest;
		const int resized_width = MAX(1, (int)Math::round((double)width * scale));
		const int resized_height = MAX(1, (int)Math::round((double)height * scale));
		p_image->resize(resized_width, resized_height, Image::INTERPOLATE_BILINEAR);
	}
	if (p_mipmaps && !p_image->has_mipmaps()) {
		p_image->generate_mipmaps(p_normal_map);
	}
}

Ref<Image> godot::compose_rgb_albedo(const Ref<Image> &p_r, const Ref<Image> &p_g, const Ref<Image> &p_b) {
	if (p_r.is_null() || p_g.is_null() || p_b.is_null()) {
		return Ref<Image>();
	}

	const int w = p_r->get_width();
	const int h = p_r->get_height();
	if (w <= 0 || h <= 0 ||
			p_g->get_width() != w || p_g->get_height() != h ||
			p_b->get_width() != w || p_b->get_height() != h) {
		UtilityFunctions::push_warning("[ProcCity] R/G/B channel maps have mismatched sizes; cannot compose albedo.");
		return Ref<Image>();
	}

	struct RedChannel {
		Ref<Image> image;
		PackedByteArray data;
		int stride = 0;
	};
	auto channel = [](const Ref<Image> &p_source) {
		RedChannel out;
		out.image = p_source;
		switch (out.image->get_format()) {
			case Image::FORMAT_L8:
			case Image::FORMAT_R8:
				out.stride = 1;
				break;
			case Image::FORMAT_LA8:
			case Image::FORMAT_RG8:
				out.stride = 2;
				break;
			case Image::FORMAT_RGB8:
				out.stride = 3;
				break;
			case Image::FORMAT_RGBA8:
				out.stride = 4;
				break;
			default:
				out.image = p_source->duplicate();
				out.image->convert(Image::FORMAT_RGBA8);
				out.stride = 4;
				break;
		}
		out.data = out.image->get_data();
		return out;
	};

	const RedChannel rd = channel(p_r);
	const RedChannel gd = channel(p_g);
	const RedChannel bd = channel(p_b);
	const uint8_t *rp = rd.data.ptr();
	const uint8_t *gp = gd.data.ptr();
	const uint8_t *bp = bd.data.ptr();

	const int64_t n = (int64_t)w * (int64_t)h;
	PackedByteArray out;
	out.resize(n * 3);
	uint8_t *op = out.ptrw();
	parallel_for_rows(h, [&](int p_begin, int p_end) {
		const int64_t begin = (int64_t)p_begin * (int64_t)w;
		const int64_t end = (int64_t)p_end * (int64_t)w;
		for (int64_t i = begin; i < end; i++) {
			op[i * 3 + 0] = rp[i * rd.stride];
			op[i * 3 + 1] = gp[i * gd.stride];
			op[i * 3 + 2] = bp[i * bd.stride];
		}
	});
	return Image::create_from_data(w, h, false, Image::FORMAT_RGB8, out);
}
