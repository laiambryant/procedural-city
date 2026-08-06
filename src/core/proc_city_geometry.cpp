#include "core/proc_city_generator.h"

#include "meshing/heightmap_mesher.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/csg_combiner3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/grid_map.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// Godot rebuilds the whole CSG boolean union on every change, so box counts
// past this stall the editor noticeably; warn before building.
static constexpr int64_t CSG_BOX_WARNING_THRESHOLD = 2048;

static MeshInstance3D *wrap_mesh_in_instance(const Ref<ArrayMesh> &p_mesh) {
	MeshInstance3D *mi = memnew(MeshInstance3D);
	mi->set_mesh(p_mesh);
	return mi;
}

bool ProcCityGenerator::_build_geometry_main() {
	if (_height_image.is_null()) {
		_emit_failed("geometry", "No displacement image; run Generate Displacement first.");
		return false;
	}
	const String budget_error = _cell_budget_error();
	if (!budget_error.is_empty()) {
		_emit_failed("geometry", budget_error);
		return false;
	}
	// A synchronous rebuild is also the explicit refresh boundary for callers
	// that mutate an Image in place before invoking build_geometry().
	_cell_heights_cache.clear();

	Node3D *container = _make_geometry_container();
	if (container == nullptr) {
		return false;
	}
	_install_geometry(container);
	return true;
}

Node3D *ProcCityGenerator::_make_geometry_container() {
	switch (build_mode) {
		case BUILD_MULTIMESH:
			return _make_multimesh_node();
		case BUILD_CSG:
			return _make_csg_node();
		case BUILD_HEX:
			return _make_hex_node();
		case BUILD_GRIDMAP:
			return _make_gridmap_node();
		default:
			return _make_blocks_node();
	}
}

Node3D *ProcCityGenerator::_make_multimesh_node() {
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	Ref<MultiMesh> mm = mesher->build_multimesh(_height_image, mesh_size, grid_vertices, height_scale, base_height, sample_filter, height_power, block_inset);
	if (mm.is_null()) {
		_emit_failed("geometry", "MultiMesh build failed.");
		return nullptr;
	}
	MultiMeshInstance3D *mmi = memnew(MultiMeshInstance3D);
	mmi->set_multimesh(mm);
	return mmi;
}

Node3D *ProcCityGenerator::_make_csg_node() {
	const int cols = MAX(1, grid_vertices.x - 1);
	const int rows = MAX(1, grid_vertices.y - 1);
	const int64_t cell_count = (int64_t)cols * (int64_t)rows;
	if (cell_count > CSG_BOX_WARNING_THRESHOLD) {
		UtilityFunctions::push_warning(String("[ProcCity] Building ") + String::num_int64(cell_count) + String(" CSG boxes; CSG rebuilds are expensive and may stall the editor."));
	}
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	CSGCombiner3D *comb = memnew(CSGCombiner3D);
	mesher->build_csg(comb, _height_image, mesh_size, grid_vertices, height_scale, base_height, sample_filter, height_power, block_inset);
	return comb;
}

Node3D *ProcCityGenerator::_make_hex_node() {
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	Ref<ArrayMesh> mesh = mesher->build_hex_mesh(_height_image, mesh_size, grid_vertices, height_scale, base_height,
												 sample_filter, hive_warp, hive_jitter, hive_gap, _resolved_seed,
												 hive_flat_rect, hive_rim_boost, hive_rim_falloff,
												 height_power, ao_strength, color_variation, hive_floor);
	if (mesh.is_null()) {
		_emit_failed("geometry", "Hex hive build failed.");
		return nullptr;
	}
	return wrap_mesh_in_instance(mesh);
}

Node3D *ProcCityGenerator::_make_blocks_node() {
	Ref<HeightmapMesher> mesher;
	mesher.instantiate();
	std::vector<float> heights;
	Ref<ArrayMesh> mesh = mesher->build_array_mesh_with_heights(
			_height_image, mesh_size, grid_vertices, height_scale, base_height, sample_filter,
			height_power, block_inset, _resolved_seed, ao_strength, color_variation,
			clip_below_height, heights);
	if (mesh.is_null()) {
		_emit_failed("geometry", "ArrayMesh build failed.");
		return nullptr;
	}
	_cache_cell_heights(heights);
	return wrap_mesh_in_instance(mesh);
}

bool ProcCityGenerator::_apply_result_geometry(const Dictionary &p_result) {
	if (p_result.has("geometry_mesh") && _result_matches_height_inputs(p_result)) {
		Ref<ArrayMesh> mesh = p_result["geometry_mesh"];
		_install_geometry(wrap_mesh_in_instance(mesh));
		return true;
	}
	return _build_geometry_main();
}

// _forget_generated_library drops the tracked MeshLibrary once the city it
// belonged to is gone. Only a GridMap has one to retint, and only the one it
// just built (_resolve_gridmap_library already cleared this for a user-supplied
// library), so any other container means the reference is dead weight.
void ProcCityGenerator::_forget_generated_library(Node3D *p_container) {
	if (Object::cast_to<GridMap>(p_container) == nullptr) {
		_generated_library = Ref<MeshLibrary>();
	}
}

void ProcCityGenerator::_reparent_into_edited_scene(Node3D *p_container) {
	if (!persist_in_scene || !Engine::get_singleton()->is_editor_hint() || get_tree() == nullptr) {
		return;
	}
	Node *root = get_tree()->get_edited_scene_root();
	if (root != nullptr) {
		_set_owner_recursive(p_container, root);
	}
}

void ProcCityGenerator::_install_geometry(Node3D *p_container) {
	Node *old = _get_generated();
	if (old != nullptr) {
		remove_child(old);
		old->queue_free();
	}

	p_container->set_name(GENERATED_NAME);
	add_child(p_container);
	_forget_generated_library(p_container);

	if (generate_collision) {
		_attach_collision(p_container);
	}
	_reparent_into_edited_scene(p_container);

	if (_material.is_valid()) {
		_apply_material_to_generated();
	}
}
