#ifndef PROC_CITY_GENERATOR_H
#define PROC_CITY_GENERATOR_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "goplacementx_params.h"

namespace godot {

// ProcCityGenerator drives the goplacementx CLI to produce a displacement map,
// builds extruded block geometry from it (ArrayMesh / MultiMesh / CSG), and
// applies a CLI-generated albedo + normal material to the result.
class ProcCityGenerator : public Node3D {
	GDCLASS(ProcCityGenerator, Node3D)

public:
	enum BuildMode {
		BUILD_ARRAY_MESH = 0,
		BUILD_MULTIMESH = 1,
		BUILD_CSG = 2,
		BUILD_HEX = 3,
	};
	enum MaterialMode {
		MATERIAL_STANDARD = 0,
		MATERIAL_ORM = 1,
	};
	// TextureMode mirrors the Blender material graph from the design: a single
	// colour texture, one texture shared across all channels, or independent maps
	// routed into the R/G/B/Roughness/Height channels.
	enum TextureMode {
		TEX_SINGLE = 0, // one colour map -> Albedo; scalar roughness/metallic
		TEX_SHARED = 1, // one map -> Albedo (colour) + Roughness + Height
		TEX_CHANNELS = 2, // five maps -> R, G, B, Roughness, Height
	};

private:
	enum Stage {
		STAGE_HEIGHT = 1,
		STAGE_GEOMETRY = 2,
		STAGE_MATERIAL = 4,
		STAGE_APPLY_MATERIAL = 8,
	};

	Ref<GoplacementxParams> params;
	Vector2 mesh_size = Vector2(10, 10);
	Vector2i grid_vertices = Vector2i(32, 32);
	double height_scale = 2.0;
	double base_height = 0.0;
	int build_mode = BUILD_ARRAY_MESH;
	int sample_filter = 1;
	int max_cells = 8192;
	// Hex hive backend (BUILD_HEX): lattice warp, per-cell irregularity and
	// visible cell separation. Seeded from the resolved generation seed.
	double hive_warp = 0.35;
	double hive_jitter = 0.45;
	double hive_gap = 0.06;
	// Rim: columns outside hive_flat_rect (mesh-local XZ) rise by up to
	// hive_rim_boost over hive_rim_falloff metres. Empty rect / zero boost
	// disables the rim entirely.
	Rect2 hive_flat_rect;
	double hive_rim_boost = 0.0;
	double hive_rim_falloff = 24.0;
	int material_mode = MATERIAL_STANDARD;
	int texture_mode = TEX_SINGLE;
	double normal_strength = 1.0;
	Vector2 uv_scale = Vector2(1, 1);
	double roughness = 1.0;
	double metallic = 0.0;
	int texture_filter = 1;
	bool texture_repeat = true;
	bool keep_intermediate_png = false;
	bool persist_in_scene = true;
	String binary_path_override;
	String output_dir;

	Ref<Image> _height_image;
	Ref<Image> _albedo_image;
	Ref<Image> _normal_image;
	// Per-channel maps used by TEX_CHANNELS (R/G/B/Roughness).
	Ref<Image> _r_image;
	Ref<Image> _g_image;
	Ref<Image> _b_image;
	Ref<Image> _rough_image;
	Ref<Material> _material;
	String _last_height_path;
	String _last_albedo_path;
	String _last_normal_path;
	// Texture mode the cached material images were generated for; drives how
	// _apply_material_main routes them onto the material.
	int _material_texture_mode = TEX_SINGLE;
	int64_t _resolved_seed = 0;
	Ref<Thread> _worker;
	bool _busy = false;

	void _ensure_params();
	String _resolve_output_dir() const;
	Node *_get_generated() const;
	void _set_owner_recursive(Node *p_node, Node *p_owner);
	bool _build_geometry_main();
	// Swaps p_container in as the GeneratedCity child (removes the old one,
	// handles editor ownership and the material override). Main thread only.
	void _install_geometry(Node3D *p_container);
	void _apply_material_main();
	Ref<Image> _compose_rgb_albedo();
	void _start_pipeline(int p_stages, bool p_fresh_seed);

	void _thread_body(Dictionary p_job);
	void _apply_results(Dictionary p_result);
	void _emit_failed(String p_stage, String p_message);
	void _cleanup_temp(const Dictionary &p_result);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_params(const Ref<GoplacementxParams> &p_params);
	Ref<GoplacementxParams> get_params() const;
	void set_mesh_size(const Vector2 &p_size) { mesh_size = p_size; }
	Vector2 get_mesh_size() const { return mesh_size; }
	void set_grid_vertices(const Vector2i &p_v) { grid_vertices = p_v; }
	Vector2i get_grid_vertices() const { return grid_vertices; }
	void set_height_scale(double p_v) { height_scale = p_v; }
	double get_height_scale() const { return height_scale; }
	void set_base_height(double p_v) { base_height = p_v; }
	double get_base_height() const { return base_height; }
	void set_build_mode(int p_v) { build_mode = p_v; }
	int get_build_mode() const { return build_mode; }
	void set_sample_filter(int p_v) { sample_filter = p_v; }
	int get_sample_filter() const { return sample_filter; }
	void set_max_cells(int p_v) { max_cells = p_v; }
	int get_max_cells() const { return max_cells; }
	void set_hive_warp(double p_v) { hive_warp = p_v; }
	double get_hive_warp() const { return hive_warp; }
	void set_hive_jitter(double p_v) { hive_jitter = p_v; }
	double get_hive_jitter() const { return hive_jitter; }
	void set_hive_gap(double p_v) { hive_gap = p_v; }
	double get_hive_gap() const { return hive_gap; }
	void set_hive_flat_rect(const Rect2 &p_v) { hive_flat_rect = p_v; }
	Rect2 get_hive_flat_rect() const { return hive_flat_rect; }
	void set_hive_rim_boost(double p_v) { hive_rim_boost = p_v; }
	double get_hive_rim_boost() const { return hive_rim_boost; }
	void set_hive_rim_falloff(double p_v) { hive_rim_falloff = p_v; }
	double get_hive_rim_falloff() const { return hive_rim_falloff; }
	void set_material_mode(int p_v) { material_mode = p_v; }
	int get_material_mode() const { return material_mode; }
	void set_texture_mode(int p_v) { texture_mode = p_v; }
	int get_texture_mode() const { return texture_mode; }
	void set_normal_strength(double p_v) { normal_strength = p_v; }
	double get_normal_strength() const { return normal_strength; }
	void set_uv_scale(const Vector2 &p_v) { uv_scale = p_v; }
	Vector2 get_uv_scale() const { return uv_scale; }
	void set_roughness(double p_v) { roughness = p_v; }
	double get_roughness() const { return roughness; }
	void set_metallic(double p_v) { metallic = p_v; }
	double get_metallic() const { return metallic; }
	void set_texture_filter(int p_v) { texture_filter = p_v; }
	int get_texture_filter() const { return texture_filter; }
	void set_texture_repeat(bool p_v) { texture_repeat = p_v; }
	bool get_texture_repeat() const { return texture_repeat; }
	void set_keep_intermediate_png(bool p_v) { keep_intermediate_png = p_v; }
	bool get_keep_intermediate_png() const { return keep_intermediate_png; }
	void set_persist_in_scene(bool p_v) { persist_in_scene = p_v; }
	bool get_persist_in_scene() const { return persist_in_scene; }
	void set_binary_path_override(const String &p_v) { binary_path_override = p_v; }
	String get_binary_path_override() const { return binary_path_override; }
	void set_output_dir(const String &p_v) { output_dir = p_v; }
	String get_output_dir() const { return output_dir; }

	void generate_displacement();
	void build_geometry();
	void generate_material();
	void generate_all();
	void randomize_palette();
	void clear_generated();

	bool is_busy() const { return _busy; }

	Callable _btn_generate_displacement() const;
	Callable _btn_build_geometry() const;
	Callable _btn_generate_material() const;
	Callable _btn_generate_all() const;
	Callable _btn_randomize_palette() const;
	Callable _btn_clear_generated() const;

	ProcCityGenerator();
	~ProcCityGenerator();
};

} // namespace godot

VARIANT_ENUM_CAST(godot::ProcCityGenerator::BuildMode);
VARIANT_ENUM_CAST(godot::ProcCityGenerator::MaterialMode);
VARIANT_ENUM_CAST(godot::ProcCityGenerator::TextureMode);

#endif // PROC_CITY_GENERATOR_H
