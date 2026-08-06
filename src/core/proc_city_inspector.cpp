#include "core/proc_city_generator.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

// _bind_inspector_surface declares everything the editor shows: the property
// groups in the order they appear in the inspector, the action buttons, the
// signals, and the enum constants scripts match those properties against.
void ProcCityGenerator::_bind_inspector_surface() {
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "params", PROPERTY_HINT_RESOURCE_TYPE, "GoplacementxParams"), "set_params", "get_params");

	ADD_GROUP("Generation", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "generation_mode", PROPERTY_HINT_ENUM, "CPU,GPU"), "set_generation_mode", "get_generation_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_gpu_server"), "set_use_gpu_server", "get_use_gpu_server");

	ADD_GROUP("Mesh", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "mesh_size", PROPERTY_HINT_NONE, "suffix:m"), "set_mesh_size", "get_mesh_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "grid_vertices", PROPERTY_HINT_RANGE, "2,256,1"), "set_grid_vertices", "get_grid_vertices");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_scale", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_height_scale", "get_height_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_height", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_base_height", "get_base_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_power", PROPERTY_HINT_RANGE, "0.1,8,0.01"), "set_height_power", "get_height_power");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "block_inset", PROPERTY_HINT_RANGE, "0,0.45,0.01"), "set_block_inset", "get_block_inset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "clip_below_height", PROPERTY_HINT_RANGE, "0,100,0.01,suffix:m"), "set_clip_below_height", "get_clip_below_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "build_mode", PROPERTY_HINT_ENUM, "ArrayMesh,MultiMesh,CSG,HexHive,GridMap"), "set_build_mode", "get_build_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sample_filter", PROPERTY_HINT_ENUM, "Nearest,BoxAverage"), "set_sample_filter", "get_sample_filter");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_cells", PROPERTY_HINT_RANGE, "1,1048576,1"), "set_max_cells", "get_max_cells");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "persist_in_scene"), "set_persist_in_scene", "get_persist_in_scene");

	ADD_GROUP("Culling", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "geometry_chunks", PROPERTY_HINT_RANGE, "1,16,1"), "set_geometry_chunks", "get_geometry_chunks");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "generate_occluders"), "set_generate_occluders", "get_generate_occluders");

	ADD_GROUP("Collision", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "generate_collision"), "set_generate_collision", "get_generate_collision");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_layer", "get_collision_layer");

	ADD_GROUP("Style", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ao_strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_ao_strength", "get_ao_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "color_variation", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_color_variation", "get_color_variation");

	ADD_GROUP("Hive", "hive_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hive_warp", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_hive_warp", "get_hive_warp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hive_jitter", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_hive_jitter", "get_hive_jitter");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hive_gap", PROPERTY_HINT_RANGE, "0,0.5,0.01"), "set_hive_gap", "get_hive_gap");
	ADD_PROPERTY(PropertyInfo(Variant::RECT2, "hive_flat_rect"), "set_hive_flat_rect", "get_hive_flat_rect");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hive_rim_boost", PROPERTY_HINT_RANGE, "0,100,0.1"), "set_hive_rim_boost", "get_hive_rim_boost");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hive_rim_falloff", PROPERTY_HINT_RANGE, "1,200,0.5"), "set_hive_rim_falloff", "get_hive_rim_falloff");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hive_floor"), "set_hive_floor", "get_hive_floor");

	ADD_GROUP("GridMap", "gridmap_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gridmap_level_height", PROPERTY_HINT_RANGE, "0,100,0.01,suffix:m"), "set_gridmap_level_height", "get_gridmap_level_height");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "gridmap_fill_columns"), "set_gridmap_fill_columns", "get_gridmap_fill_columns");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "gridmap_mesh_library", PROPERTY_HINT_RESOURCE_TYPE, "MeshLibrary"), "set_gridmap_mesh_library", "get_gridmap_mesh_library");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "gridmap_item_id"), "set_gridmap_item_id", "get_gridmap_item_id");

	ADD_GROUP("Material", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_mode", PROPERTY_HINT_ENUM, "Standard,ORM"), "set_material_mode", "get_material_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_mode", PROPERTY_HINT_ENUM, "Single,Shared,Channels"), "set_texture_mode", "get_texture_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_normal_strength", "get_normal_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_roughness", "get_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "metallic", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_metallic", "get_metallic");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "uv_scale"), "set_uv_scale", "get_uv_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_filter", PROPERTY_HINT_ENUM, "Nearest,Linear,Linear Mipmap Anisotropic"), "set_texture_filter", "get_texture_filter");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_max_size", PROPERTY_HINT_RANGE, "0,16384,1,or_greater,suffix:px"), "set_material_max_size", "get_material_max_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "texture_repeat"), "set_texture_repeat", "get_texture_repeat");

	ADD_GROUP("Tool", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_download_binary"), "set_auto_download_binary", "get_auto_download_binary");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "binary_path_override", PROPERTY_HINT_GLOBAL_FILE), "set_binary_path_override", "get_binary_path_override");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "output_dir", PROPERTY_HINT_GLOBAL_DIR), "set_output_dir", "get_output_dir");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "keep_intermediate_png"), "set_keep_intermediate_png", "get_keep_intermediate_png");

	ADD_GROUP("Actions", "");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "generate_displacement_action", PROPERTY_HINT_TOOL_BUTTON, "Generate Displacement", PROPERTY_USAGE_EDITOR), "", "_btn_generate_displacement");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "build_geometry_action", PROPERTY_HINT_TOOL_BUTTON, "Build Geometry", PROPERTY_USAGE_EDITOR), "", "_btn_build_geometry");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "generate_material_action", PROPERTY_HINT_TOOL_BUTTON, "Generate Material", PROPERTY_USAGE_EDITOR), "", "_btn_generate_material");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "generate_all_action", PROPERTY_HINT_TOOL_BUTTON, "Generate All", PROPERTY_USAGE_EDITOR), "", "_btn_generate_all");
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "clear_generated_action", PROPERTY_HINT_TOOL_BUTTON, "Clear Generated", PROPERTY_USAGE_EDITOR), "", "_btn_clear_generated");

	ADD_SIGNAL(MethodInfo("generation_started", PropertyInfo(Variant::STRING, "stage")));
	ADD_SIGNAL(MethodInfo("generation_progress", PropertyInfo(Variant::STRING, "stage"), PropertyInfo(Variant::FLOAT, "ratio")));
	ADD_SIGNAL(MethodInfo("generation_finished", PropertyInfo(Variant::STRING, "stage"), PropertyInfo(Variant::STRING, "path")));
	ADD_SIGNAL(MethodInfo("generation_failed", PropertyInfo(Variant::STRING, "stage"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("all_finished"));

	BIND_ENUM_CONSTANT(GEN_CPU);
	BIND_ENUM_CONSTANT(GEN_GPU);
	BIND_ENUM_CONSTANT(BUILD_ARRAY_MESH);
	BIND_ENUM_CONSTANT(BUILD_MULTIMESH);
	BIND_ENUM_CONSTANT(BUILD_CSG);
	BIND_ENUM_CONSTANT(BUILD_HEX);
	BIND_ENUM_CONSTANT(BUILD_GRIDMAP);
	BIND_ENUM_CONSTANT(MATERIAL_STANDARD);
	BIND_ENUM_CONSTANT(MATERIAL_ORM);
	BIND_ENUM_CONSTANT(TEX_SINGLE);
	BIND_ENUM_CONSTANT(TEX_SHARED);
	BIND_ENUM_CONSTANT(TEX_CHANNELS);
	BIND_ENUM_CONSTANT(TEXTURE_FILTER_NEAREST);
	BIND_ENUM_CONSTANT(TEXTURE_FILTER_LINEAR);
	BIND_ENUM_CONSTANT(TEXTURE_FILTER_LINEAR_MIPMAP_ANISOTROPIC);
}

Callable ProcCityGenerator::_btn_generate_displacement() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "generate_displacement");
}
Callable ProcCityGenerator::_btn_build_geometry() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "build_geometry");
}
Callable ProcCityGenerator::_btn_generate_material() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "generate_material");
}
Callable ProcCityGenerator::_btn_generate_all() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "generate_all");
}
Callable ProcCityGenerator::_btn_randomize_palette() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "randomize_palette");
}
Callable ProcCityGenerator::_btn_clear_generated() const {
	return Callable(const_cast<ProcCityGenerator *>(this), "clear_generated");
}
