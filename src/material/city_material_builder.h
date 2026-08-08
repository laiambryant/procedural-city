#ifndef CITY_MATERIAL_BUILDER_H
#define CITY_MATERIAL_BUILDER_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot {

struct CityMaterialSpec {
	bool orm = false;
	Ref<Image> albedo;
	Ref<Image> normal_map;
	Ref<Image> roughness_map;
	double normal_strength = 1.0;
	double roughness = 1.0;
	double metallic = 0.0;
	Vector2 uv_scale = Vector2(1, 1);
	int texture_filter = 1;
	bool repeat = true;
};

Ref<Material> build_city_material(const CityMaterialSpec &p_spec);

void prepare_material_image(const Ref<Image> &p_image, int p_max_size, bool p_mipmaps, bool p_normal_map = false);

Ref<Image> compose_rgb_albedo(const Ref<Image> &p_r, const Ref<Image> &p_g, const Ref<Image> &p_b);

} // namespace godot

#endif // CITY_MATERIAL_BUILDER_H
