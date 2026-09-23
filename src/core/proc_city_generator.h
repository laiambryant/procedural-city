#ifndef PROC_CITY_GENERATOR_H
#define PROC_CITY_GENERATOR_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh_library.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

#include "cli/goplacementx_params.h"
#include "meshing/heightmap_mesher.h"

namespace godot {

class GridMap;
class MeshInstance3D;
struct CityMaterialSpec;

inline constexpr const char *GENERATED_NAME = "GeneratedCity";

inline constexpr const char *GENERATION_MODE_HINT = "GPU,CPU,GPU (Legacy),CPU (Legacy)";

class ProcCityGenerator : public Node3D {
	GDCLASS(ProcCityGenerator, Node3D)

public:
	enum GenerationMode {
		GEN_GPU_NATIVE = 0,
		GEN_CPU_NATIVE = 1,
		GEN_GPU_LEGACY = 2,
		GEN_CPU_LEGACY = 3,
	};
	enum Stage {
		STAGE_HEIGHT = 1,
		STAGE_GEOMETRY = 2,
		STAGE_MATERIAL = 4,
		STAGE_APPLY_MATERIAL = 8,
	};
	enum BuildMode {
		BUILD_ARRAY_MESH = 0,
		BUILD_MULTIMESH = 1,
		BUILD_CSG = 2,
		BUILD_HEX = 3,
		BUILD_GRIDMAP = 4,
	};
	enum MaterialMode {
		MATERIAL_STANDARD = 0,
		MATERIAL_ORM = 1,
	};
	enum TextureMode {
		TEX_SINGLE = 0,
		TEX_SHARED = 1,
		TEX_CHANNELS = 2,
	};
	enum TextureFilter {
		TEXTURE_FILTER_NEAREST = 0,
		TEXTURE_FILTER_LINEAR = 1,
		TEXTURE_FILTER_LINEAR_MIPMAP_ANISOTROPIC = 2,
	};

private:
	Ref<GoplacementxParams> params;
	int generation_mode = GEN_GPU_NATIVE;
	Vector2 mesh_size = Vector2(10, 10);
	Vector2i grid_vertices = Vector2i(32, 32);
	double height_scale = 2.0;
	double base_height = 0.0;
	double height_power = 1.0;
	double block_inset = 0.0;
	double clip_below_height = 0.0;
	int build_mode = BUILD_ARRAY_MESH;
	int sample_filter = 1;
	int max_cells = 8192;
	int geometry_chunks = 1;
	bool generate_occluders = false;
	double hive_warp = 0.35;
	double hive_jitter = 0.45;
	double hive_gap = 0.06;
	Rect2 hive_flat_rect;
	TypedArray<Rect2> carve_rects;
	double hive_rim_boost = 0.0;
	double hive_rim_falloff = 24.0;
	bool hive_floor = true;
	double gridmap_level_height = 0.0;
	bool gridmap_fill_columns = true;
	Ref<MeshLibrary> gridmap_mesh_library;
	int gridmap_item_id = 0;
	double ao_strength = 0.7;
	double color_variation = 0.12;
	int material_mode = MATERIAL_STANDARD;
	int texture_mode = TEX_SINGLE;
	double normal_strength = 1.0;
	Vector2 uv_scale = Vector2(1, 1);
	double roughness = 1.0;
	double metallic = 0.0;
	int texture_filter = 1;
	int material_max_size = 0;
	bool texture_repeat = true;
	bool keep_intermediate_png = false;
	bool persist_in_scene = true;
	String external_resource_dir = "res://generated_city";
	bool generate_collision = false;
	int collision_layer = 1;
	bool auto_download_binary = true;
	bool use_gpu_server = false;
	String binary_path_override;
	String output_dir;

	Ref<Image> _height_image;
	Ref<Image> _albedo_image;
	Ref<Image> _normal_image;
	Ref<Image> _r_image;
	Ref<Image> _g_image;
	Ref<Image> _b_image;
	Ref<Image> _rough_image;
	Ref<Material> _material;
	mutable Dictionary _cell_heights_cache;
	int64_t _height_inputs_revision = 0;
	Ref<MeshLibrary> _generated_library;
	String _last_height_path;
	String _last_albedo_path;
	String _last_normal_path;
	int _material_texture_mode = TEX_SINGLE;
	int _material_texture_filter = TEXTURE_FILTER_LINEAR;
	int64_t _resolved_seed = 0;
	uint64_t _pipeline_started_usec = 0;
	int _last_generation_used = GEN_GPU_NATIVE;
	Ref<Thread> _worker;
	bool _busy = false;

	void _ensure_params();
	bool _runs_standalone_cli() const;
	bool _bakes_style_into_vertices() const;
	bool _roughness_comes_from_a_map() const;
	bool _property_is_usable(const String &p_name) const;
	String _resolve_output_dir() const;
	Node *_get_generated() const;
	void _set_owner_recursive(Node *p_node, Node *p_owner);
	String _cell_budget_error() const;
	void _invalidate_cell_heights_cache();
	void _cache_cell_heights(const std::vector<float> &p_heights) const;
	bool _result_matches_height_inputs(const Dictionary &p_result) const;

	bool _build_geometry_main();
	Node3D *_make_geometry_container();
	Node3D *_make_multimesh_node();
	Node3D *_make_csg_node();
	Node3D *_make_hex_node();
	Node3D *_make_blocks_node();
	Node3D *_make_gridmap_node();
	Node3D *_make_chunk_container(const std::vector<Ref<ArrayMesh>> &p_meshes);
	Node3D *_install_worker_meshes(const TypedArray<ArrayMesh> &p_meshes);
	bool _resolve_gridmap_library(const Vector3 &p_cell_size, Ref<MeshLibrary> &r_library, int &r_item);
	void _install_geometry(Node3D *p_container);
	void _forget_generated_library(Node3D *p_container);
	Node *_edited_scene_root_for_persistence() const;
	void _reparent_into_edited_scene(Node3D *p_container);
	void _externalize_when_scene_owned();
	void _attach_collision(Node3D *p_container);
	void _attach_trimesh_body(MeshInstance3D *p_mesh_instance);
	void _attach_multimesh_body(Node3D *p_container);

	CityMaterialSpec _material_spec();
	void _apply_material_main();
	void _release_material_images();
	Ref<Image> _resolve_albedo_image();
	Ref<Image> _resolve_roughness_image() const;
	void _apply_material_to_generated();
	void _retint_generated_library(GridMap *p_gridmap);

	void _start_pipeline(int p_stages, bool p_fresh_seed);
	static String _pipeline_label(int p_stages);
	Dictionary _snapshot_job(int p_stages) const;
	void _thread_body(Dictionary p_job);
	void _apply_results(Dictionary p_result);
	void _store_result_images(const Dictionary &p_result);
	bool _apply_result_geometry(const Dictionary &p_result);
	void _join_worker();
	void _finish_worker();
	void _emit_failed(String p_stage, String p_message);
	void _cleanup_temp(const Dictionary &p_result);

protected:
	static void _bind_methods();
	static void _bind_inspector_surface();
	void _validate_property(PropertyInfo &p_property) const;
	void _notification(int p_what);

public:
	void set_params(const Ref<GoplacementxParams> &p_params);
	Ref<GoplacementxParams> get_params() const;
	void set_mesh_size(const Vector2 &p_size) {
		mesh_size = p_size;
		_invalidate_cell_heights_cache();
	}
	Vector2 get_mesh_size() const { return mesh_size; }
	void set_grid_vertices(const Vector2i &p_v) {
		grid_vertices = p_v;
		_invalidate_cell_heights_cache();
	}
	Vector2i get_grid_vertices() const { return grid_vertices; }
	void set_height_scale(double p_v) {
		height_scale = p_v;
		_invalidate_cell_heights_cache();
	}
	double get_height_scale() const { return height_scale; }
	void set_base_height(double p_v) {
		base_height = p_v;
		_invalidate_cell_heights_cache();
	}
	double get_base_height() const { return base_height; }
	void set_height_power(double p_v) {
		height_power = p_v;
		_invalidate_cell_heights_cache();
	}
	double get_height_power() const { return height_power; }
	void set_block_inset(double p_v) { block_inset = p_v; }
	double get_block_inset() const { return block_inset; }
	void set_clip_below_height(double p_v) { clip_below_height = p_v; }
	double get_clip_below_height() const { return clip_below_height; }
	void set_build_mode(int p_v) {
		build_mode = p_v;
		notify_property_list_changed();
	}
	int get_build_mode() const { return build_mode; }
	void set_generation_mode(int p_v) {
		generation_mode = p_v;
		notify_property_list_changed();
	}
	int get_generation_mode() const { return generation_mode; }
	int get_last_generation_mode_used() const { return _last_generation_used; }
	void set_sample_filter(int p_v) {
		sample_filter = p_v;
		_invalidate_cell_heights_cache();
	}
	int get_sample_filter() const { return sample_filter; }
	void set_max_cells(int p_v) { max_cells = p_v; }
	int get_max_cells() const { return max_cells; }
	void set_geometry_chunks(int p_v) { geometry_chunks = CLAMP(p_v, 1, MAX_GEOMETRY_CHUNKS); }
	int get_geometry_chunks() const { return geometry_chunks; }
	void set_generate_occluders(bool p_v) { generate_occluders = p_v; }
	bool get_generate_occluders() const { return generate_occluders; }
	void set_hive_warp(double p_v) { hive_warp = p_v; }
	double get_hive_warp() const { return hive_warp; }
	void set_hive_jitter(double p_v) { hive_jitter = p_v; }
	double get_hive_jitter() const { return hive_jitter; }
	void set_hive_gap(double p_v) { hive_gap = p_v; }
	double get_hive_gap() const { return hive_gap; }
	void set_hive_flat_rect(const Rect2 &p_v) { hive_flat_rect = p_v; }
	Rect2 get_hive_flat_rect() const { return hive_flat_rect; }
	void set_hive_rim_boost(double p_v) {
		hive_rim_boost = p_v;
		notify_property_list_changed();
	}
	double get_hive_rim_boost() const { return hive_rim_boost; }
	void set_hive_rim_falloff(double p_v) { hive_rim_falloff = p_v; }
	double get_hive_rim_falloff() const { return hive_rim_falloff; }
	void set_hive_floor(bool p_v) { hive_floor = p_v; }
	bool get_hive_floor() const { return hive_floor; }
	void set_gridmap_level_height(double p_v) { gridmap_level_height = p_v; }
	double get_gridmap_level_height() const { return gridmap_level_height; }
	void set_gridmap_fill_columns(bool p_v) { gridmap_fill_columns = p_v; }
	bool get_gridmap_fill_columns() const { return gridmap_fill_columns; }
	void set_gridmap_mesh_library(const Ref<MeshLibrary> &p_v) {
		gridmap_mesh_library = p_v;
		notify_property_list_changed();
	}
	Ref<MeshLibrary> get_gridmap_mesh_library() const { return gridmap_mesh_library; }
	void set_gridmap_item_id(int p_v) { gridmap_item_id = p_v; }
	int get_gridmap_item_id() const { return gridmap_item_id; }
	void set_ao_strength(double p_v) { ao_strength = p_v; }
	double get_ao_strength() const { return ao_strength; }
	void set_color_variation(double p_v) { color_variation = p_v; }
	double get_color_variation() const { return color_variation; }
	void set_material_mode(int p_v) { material_mode = p_v; }
	int get_material_mode() const { return material_mode; }
	void set_texture_mode(int p_v) {
		texture_mode = p_v;
		notify_property_list_changed();
	}
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
	void set_material_max_size(int p_v) { material_max_size = p_v; }
	int get_material_max_size() const { return material_max_size; }
	void set_texture_repeat(bool p_v) { texture_repeat = p_v; }
	bool get_texture_repeat() const { return texture_repeat; }
	void set_keep_intermediate_png(bool p_v) { keep_intermediate_png = p_v; }
	bool get_keep_intermediate_png() const { return keep_intermediate_png; }
	void set_persist_in_scene(bool p_v) {
		persist_in_scene = p_v;
		notify_property_list_changed();
	}
	bool get_persist_in_scene() const { return persist_in_scene; }
	void set_external_resource_dir(const String &p_v) { external_resource_dir = p_v; }
	String get_external_resource_dir() const { return external_resource_dir; }
	void set_generate_collision(bool p_v) {
		generate_collision = p_v;
		notify_property_list_changed();
	}
	bool get_generate_collision() const { return generate_collision; }
	void set_collision_layer(int p_v) { collision_layer = p_v; }
	int get_collision_layer() const { return collision_layer; }
	void set_auto_download_binary(bool p_v) { auto_download_binary = p_v; }
	bool get_auto_download_binary() const { return auto_download_binary; }
	void set_use_gpu_server(bool p_v) { use_gpu_server = p_v; }
	bool get_use_gpu_server() const { return use_gpu_server; }
	void set_binary_path_override(const String &p_v) { binary_path_override = p_v; }
	String get_binary_path_override() const { return binary_path_override; }
	void set_output_dir(const String &p_v) { output_dir = p_v; }
	String get_output_dir() const { return output_dir; }

	void set_height_image(const Ref<Image> &p_image) {
		_height_image = p_image;
		_invalidate_cell_heights_cache();
	}
	Ref<Image> get_height_image() const { return _height_image; }

	void set_carve_rects(const TypedArray<Rect2> &p_v) {
		carve_rects = p_v.duplicate();
		_invalidate_cell_heights_cache();
	}
	TypedArray<Rect2> get_carve_rects() const { return carve_rects.duplicate(); }

	Dictionary get_cell_heights() const;

	double sample_city_height(const Vector3 &p_local_point) const;
	bool is_point_inside_block(const Vector3 &p_local_point) const;
	Vector3 find_clear_point(const Vector3 &p_local_point, double p_max_radius) const;

	void release_source_images();

	void externalize_generated_resources();

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

VARIANT_ENUM_CAST(godot::ProcCityGenerator::GenerationMode);
VARIANT_ENUM_CAST(godot::ProcCityGenerator::BuildMode);
VARIANT_ENUM_CAST(godot::ProcCityGenerator::MaterialMode);
VARIANT_ENUM_CAST(godot::ProcCityGenerator::TextureMode);
VARIANT_ENUM_CAST(godot::ProcCityGenerator::TextureFilter);

#endif // PROC_CITY_GENERATOR_H
