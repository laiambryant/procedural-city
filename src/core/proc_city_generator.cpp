#include "core/proc_city_generator.h"

#include "meshing/height_sampling.h"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

ProcCityGenerator::ProcCityGenerator() {}

ProcCityGenerator::~ProcCityGenerator() {
	_join_worker();
}

void ProcCityGenerator::_notification(int p_what) {
	if (p_what == NOTIFICATION_PREDELETE) {
		_join_worker();
	}
}

// _join_worker blocks until the worker thread is done. The worker reaches back
// into this node through a deferred call, so it must never outlive it.
void ProcCityGenerator::_join_worker() {
	if (_worker.is_valid() && _worker->is_started()) {
		_worker->wait_to_finish();
	}
}

void ProcCityGenerator::set_params(const Ref<GoplacementxParams> &p_params) {
	params = p_params;
}

Ref<GoplacementxParams> ProcCityGenerator::get_params() const {
	return params;
}

void ProcCityGenerator::_ensure_params() {
	if (params.is_null()) {
		params.instantiate();
	}
}

String ProcCityGenerator::_resolve_output_dir() const {
	if (!output_dir.is_empty()) {
		return ProjectSettings::get_singleton()->globalize_path(output_dir);
	}
	return OS::get_singleton()->get_cache_dir();
}

Node *ProcCityGenerator::_get_generated() const {
	return get_node_or_null(NodePath(GENERATED_NAME));
}

void ProcCityGenerator::_set_owner_recursive(Node *p_node, Node *p_owner) {
	if (p_node == nullptr || p_owner == nullptr) {
		return;
	}
	p_node->set_owner(p_owner);
	TypedArray<Node> kids = p_node->get_children();
	for (int i = 0; i < kids.size(); i++) {
		_set_owner_recursive(Object::cast_to<Node>(kids[i]), p_owner);
	}
}

String ProcCityGenerator::_cell_budget_error() const {
	const int cols = MAX(1, grid_vertices.x - 1);
	const int rows = MAX(1, grid_vertices.y - 1);
	const int64_t cell_count = (int64_t)cols * (int64_t)rows;
	if (cell_count <= (int64_t)max_cells) {
		return String();
	}
	return String("Cell count ") + String::num_int64(cell_count) + String(" exceeds Max Cells ") + String::num_int64(max_cells) + String(". Lower Grid Vertices or raise Max Cells.");
}

static PackedFloat32Array pack_heights(const std::vector<float> &p_heights) {
	PackedFloat32Array packed;
	packed.resize((int64_t)p_heights.size());
	if (!p_heights.empty()) {
		memcpy(packed.ptrw(), p_heights.data(), p_heights.size() * sizeof(float));
	}
	return packed;
}

void ProcCityGenerator::_invalidate_cell_heights_cache() {
	_cell_heights_cache.clear();
	_height_inputs_revision++;
}

void ProcCityGenerator::_cache_cell_heights(const std::vector<float> &p_heights) const {
	const CellGrid grid = make_cell_grid(mesh_size, grid_vertices);
	Dictionary result;
	result["columns"] = grid.cols;
	result["rows"] = grid.rows;
	result["cell_size"] = Vector2(grid.cw, grid.cd);
	result["origin"] = Vector2(grid.ox, grid.oz);
	result["heights"] = pack_heights(p_heights);
	_cell_heights_cache = result;
}

Dictionary ProcCityGenerator::get_cell_heights() const {
	if (_cell_heights_cache.size() > 0) {
		return _cell_heights_cache;
	}
	Dictionary result;
	if (_height_image.is_null()) {
		return result;
	}
	const CellGrid grid = make_cell_grid(mesh_size, grid_vertices);
	std::vector<float> heights;
	if (!resolve_cell_heights(_height_image, grid, height_scale, base_height, sample_filter, heights, height_power)) {
		return result;
	}
	_cache_cell_heights(heights);
	return _cell_heights_cache;
}

void ProcCityGenerator::release_source_images() {
	_height_image.unref();
	_release_material_images();
}

void ProcCityGenerator::generate_displacement() {
	_start_pipeline(STAGE_HEIGHT, true);
}

void ProcCityGenerator::build_geometry() {
	if (_busy) {
		_emit_failed("geometry", "A generation is already in progress.");
		return;
	}
	emit_signal("generation_started", "geometry");
	if (_build_geometry_main()) {
		emit_signal("generation_finished", "geometry", String());
		emit_signal("all_finished");
	}
}

void ProcCityGenerator::generate_material() {
	_start_pipeline(STAGE_MATERIAL | STAGE_APPLY_MATERIAL, false);
}

void ProcCityGenerator::generate_all() {
	_start_pipeline(STAGE_HEIGHT | STAGE_GEOMETRY | STAGE_MATERIAL | STAGE_APPLY_MATERIAL, true);
}

void ProcCityGenerator::randomize_palette() {
	_ensure_params();
	params->randomize_palette();
	if (!_busy && _height_image.is_valid()) {
		generate_material();
	}
}

void ProcCityGenerator::clear_generated() {
	Node *old = _get_generated();
	if (old != nullptr) {
		remove_child(old);
		old->queue_free();
	}
	_material = Ref<Material>();
	_generated_library = Ref<MeshLibrary>();
}
