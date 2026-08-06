#include "core/proc_city_generator.h"

#include "core/proc_city_gridmap.h"
#include "material/city_material_builder.h"

#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/grid_map.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

Ref<Image> ProcCityGenerator::_resolve_albedo_image() {
	if (_material_texture_mode == TEX_CHANNELS) {
		if (_albedo_image.is_valid()) {
			return _albedo_image;
		}
		Ref<Image> composed = compose_rgb_albedo(_r_image, _g_image, _b_image);
		if (composed.is_valid() && _material_texture_filter == TEXTURE_FILTER_LINEAR_MIPMAP_ANISOTROPIC) {
			composed->generate_mipmaps();
		}
		return composed;
	}
	return _albedo_image;
}

Ref<Image> ProcCityGenerator::_resolve_roughness_image() const {
	if (_rough_image.is_valid()) {
		return _rough_image;
	}
	if (_material_texture_mode == TEX_SHARED) {
		return _height_image;
	}
	return Ref<Image>();
}

CityMaterialSpec ProcCityGenerator::_material_spec() {
	CityMaterialSpec spec;
	spec.orm = material_mode == MATERIAL_ORM;
	spec.albedo = _resolve_albedo_image();
	spec.normal_map = _normal_image;
	spec.roughness_map = _resolve_roughness_image();
	spec.normal_strength = normal_strength;
	spec.roughness = roughness;
	spec.metallic = metallic;
	spec.uv_scale = uv_scale;
	spec.texture_filter = _material_texture_filter;
	spec.repeat = texture_repeat;
	return spec;
}

void ProcCityGenerator::_apply_material_main() {
	Ref<Material> mat = build_city_material(_material_spec());
	if (mat.is_null()) {
		UtilityFunctions::push_warning("[ProcCity] No albedo image to build a material from.");
		return;
	}
	_material = mat;
	_apply_material_to_generated();
	_release_material_images();
}

void ProcCityGenerator::_release_material_images() {
	_albedo_image.unref();
	_normal_image.unref();
	_r_image.unref();
	_g_image.unref();
	_b_image.unref();
	_rough_image.unref();
}

// _retint_generated_library reaches the one place a GridMap city's look lives.
// Only a library this node generated is retinted; a user-supplied one is
// somebody else's resource and keeps the materials it shipped with.
void ProcCityGenerator::_retint_generated_library(GridMap *p_gridmap) {
	if (_generated_library.is_valid() && p_gridmap->get_mesh_library() == _generated_library) {
		apply_library_material(_generated_library, _material);
	}
}

// A chunked city is a plain Node3D holding one MeshInstance3D per tile, so the
// override has to reach the children rather than the container. Recursing also
// covers any future container shape without another special case.
static bool override_material_recursive(Node *p_node, const Ref<Material> &p_material) {
	GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(p_node);
	if (gi != nullptr) {
		gi->set_material_override(p_material);
		return true;
	}
	bool applied = false;
	const int count = p_node->get_child_count();
	for (int i = 0; i < count; i++) {
		applied = override_material_recursive(p_node->get_child(i), p_material) || applied;
	}
	return applied;
}

void ProcCityGenerator::_apply_material_to_generated() {
	Node *generated = _get_generated();
	if (generated == nullptr) {
		return;
	}
	GridMap *gridmap = Object::cast_to<GridMap>(generated);
	if (gridmap != nullptr) {
		_retint_generated_library(gridmap);
		return;
	}
	override_material_recursive(generated, _material);
}
