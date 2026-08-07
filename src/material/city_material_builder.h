#ifndef CITY_MATERIAL_BUILDER_H
#define CITY_MATERIAL_BUILDER_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot {

// CityMaterialSpec is everything build_city_material needs to assemble a
// Standard/ORM material from CLI-generated maps. A null roughness_map falls
// back to the scalar roughness value.
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

// Shrink one shading-only source map and optionally build its mip chain. The
// image is mutated in place; callers use it immediately after decoding so a
// full-resolution material map never needs to join the retained result set.
// Normal mipmaps use Image's vector renormalization path.
void prepare_material_image(const Ref<Image> &p_image, int p_max_size, bool p_mipmaps, bool p_normal_map = false);

// compose_rgb_albedo packs the red channel of three grayscale maps into a
// single RGB8 image, reproducing the Combine Color node from the design graph.
// Returns null when the maps are missing or their sizes disagree.
Ref<Image> compose_rgb_albedo(const Ref<Image> &p_r, const Ref<Image> &p_g, const Ref<Image> &p_b);

} // namespace godot

#endif // CITY_MATERIAL_BUILDER_H
