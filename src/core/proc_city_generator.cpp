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

// CellLookup is get_cell_heights()'s payload unpacked once, so a query loop
// pays the Dictionary lookups a single time instead of per point.
struct CellLookup {
	PackedFloat32Array heights;
	int columns = 0;
	int rows = 0;
	Vector2 cell_size;
	Vector2 origin;

	bool is_valid() const { return columns > 0 && rows > 0 && heights.size() >= columns * rows; }

	// Grid coordinates of the cell containing a mesh-local XZ point, or (-1, -1)
	// when the point falls outside the field.
	Vector2i cell_at(float p_x, float p_z) const {
		if (cell_size.x <= 0.0f || cell_size.y <= 0.0f) {
			return Vector2i(-1, -1);
		}
		const int i = (int)Math::floor((p_x - origin.x) / cell_size.x);
		const int j = (int)Math::floor((p_z - origin.y) / cell_size.y);
		if (i < 0 || i >= columns || j < 0 || j >= rows) {
			return Vector2i(-1, -1);
		}
		return Vector2i(i, j);
	}
};

static CellLookup unpack_cell_heights(const Dictionary &p_cells) {
	CellLookup lookup;
	if (p_cells.is_empty()) {
		return lookup;
	}
	lookup.columns = (int)p_cells.get("columns", 0);
	lookup.rows = (int)p_cells.get("rows", 0);
	lookup.cell_size = p_cells.get("cell_size", Vector2());
	lookup.origin = p_cells.get("origin", Vector2());
	lookup.heights = p_cells.get("heights", PackedFloat32Array());
	return lookup;
}

double ProcCityGenerator::sample_city_height(const Vector3 &p_local_point) const {
	const CellLookup lookup = unpack_cell_heights(get_cell_heights());
	if (!lookup.is_valid()) {
		return 0.0;
	}
	const Vector2i cell = lookup.cell_at((float)p_local_point.x, (float)p_local_point.z);
	if (cell.x < 0) {
		return 0.0;
	}
	return (double)lookup.heights[cell.y * lookup.columns + cell.x];
}

bool ProcCityGenerator::is_point_inside_block(const Vector3 &p_local_point) const {
	const CellLookup lookup = unpack_cell_heights(get_cell_heights());
	if (!lookup.is_valid()) {
		return false;
	}
	const Vector2i cell = lookup.cell_at((float)p_local_point.x, (float)p_local_point.z);
	if (cell.x < 0) {
		return false;
	}
	const float top = lookup.heights[cell.y * lookup.columns + cell.x];
	// A cell at or below the clip height contributes no geometry at all, so
	// there is nothing there to be inside of.
	if (top <= MAX(0.0f, (float)clip_below_height)) {
		return false;
	}
	if (p_local_point.y < 0.0 || p_local_point.y > (double)top) {
		return false;
	}
	// Freestanding blocks do not fill their cell: a point in the street between
	// two towers is inside the cell but outside the building.
	const float inset = CLAMP((float)block_inset, 0.0f, MAX_BLOCK_INSET);
	if (inset <= 0.0f) {
		return true;
	}
	const float cell_x = lookup.origin.x + (float)cell.x * lookup.cell_size.x;
	const float cell_z = lookup.origin.y + (float)cell.y * lookup.cell_size.y;
	const float margin_x = lookup.cell_size.x * inset;
	const float margin_z = lookup.cell_size.y * inset;
	return (float)p_local_point.x >= cell_x + margin_x &&
			(float)p_local_point.x <= cell_x + lookup.cell_size.x - margin_x &&
			(float)p_local_point.z >= cell_z + margin_z &&
			(float)p_local_point.z <= cell_z + lookup.cell_size.y - margin_z;
}

Vector3 ProcCityGenerator::find_clear_point(const Vector3 &p_local_point, double p_max_radius) const {
	const CellLookup lookup = unpack_cell_heights(get_cell_heights());
	if (!lookup.is_valid()) {
		return p_local_point;
	}
	const float clip = MAX(0.0f, (float)clip_below_height);
	const Vector2i start = lookup.cell_at((float)p_local_point.x, (float)p_local_point.z);
	// Outside the field there is nothing to be blocked by.
	if (start.x < 0) {
		return p_local_point;
	}
	const float step = MAX(lookup.cell_size.x, lookup.cell_size.y);
	const int max_rings = step > 0.0f ? (int)Math::ceil(MAX(0.0, p_max_radius) / step) : 0;

	// Expanding ring search over the grid: the first cell whose block does not
	// stand above the clip height is the nearest place a spawn can sit.
	for (int ring = 0; ring <= max_rings; ring++) {
		for (int dj = -ring; dj <= ring; dj++) {
			for (int di = -ring; di <= ring; di++) {
				// Only the ring's border; the interior was covered by earlier rings.
				if (ring > 0 && Math::abs(di) != ring && Math::abs(dj) != ring) {
					continue;
				}
				const int i = start.x + di;
				const int j = start.y + dj;
				if (i < 0 || i >= lookup.columns || j < 0 || j >= lookup.rows) {
					continue;
				}
				if (lookup.heights[j * lookup.columns + i] > clip) {
					continue;
				}
				return Vector3(
						lookup.origin.x + ((float)i + 0.5f) * lookup.cell_size.x,
						p_local_point.y,
						lookup.origin.y + ((float)j + 0.5f) * lookup.cell_size.y);
			}
		}
	}
	return p_local_point;
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
