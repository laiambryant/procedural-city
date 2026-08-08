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

struct CellLookup {
	PackedFloat32Array heights;
	int columns = 0;
	int rows = 0;
	Vector2 cell_size;
	Vector2 origin;

	bool is_valid() const { return columns > 0 && rows > 0 && heights.size() >= columns * rows; }

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

static bool is_height_clipped(float p_height, float p_clip) {
	return p_height <= p_clip;
}

static bool point_within_inset_margin(float p_x, float p_z, const Vector2 &p_cell_origin, const Vector2 &p_cell_size, float p_inset) {
	if (p_inset <= 0.0f) {
		return true;
	}
	const float margin_x = p_cell_size.x * p_inset;
	const float margin_z = p_cell_size.y * p_inset;
	return p_x >= p_cell_origin.x + margin_x && p_x <= p_cell_origin.x + p_cell_size.x - margin_x &&
			p_z >= p_cell_origin.y + margin_z && p_z <= p_cell_origin.y + p_cell_size.y - margin_z;
}

static bool is_on_ring_border(int p_di, int p_dj, int p_ring) {
	return p_ring == 0 || Math::abs(p_di) == p_ring || Math::abs(p_dj) == p_ring;
}

static Vector2i find_nearest_clear_cell(const CellLookup &p_lookup, const Vector2i &p_start, float p_clip, int p_max_rings) {
	for (int ring = 0; ring <= p_max_rings; ring++) {
		for (int dj = -ring; dj <= ring; dj++) {
			for (int di = -ring; di <= ring; di++) {
				if (!is_on_ring_border(di, dj, ring)) {
					continue;
				}
				const int i = p_start.x + di;
				const int j = p_start.y + dj;
				if (i < 0 || i >= p_lookup.columns || j < 0 || j >= p_lookup.rows) {
					continue;
				}
				if (!is_height_clipped(p_lookup.heights[j * p_lookup.columns + i], p_clip)) {
					continue;
				}
				return Vector2i(i, j);
			}
		}
	}
	return Vector2i(-1, -1);
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
	const float clip = MAX(0.0f, (float)clip_below_height);
	if (is_height_clipped(top, clip)) {
		return false;
	}
	if (p_local_point.y < 0.0 || p_local_point.y > (double)top) {
		return false;
	}
	const float inset = CLAMP((float)block_inset, 0.0f, MAX_BLOCK_INSET);
	const Vector2 cell_origin(lookup.origin.x + (float)cell.x * lookup.cell_size.x, lookup.origin.y + (float)cell.y * lookup.cell_size.y);
	return point_within_inset_margin((float)p_local_point.x, (float)p_local_point.z, cell_origin, lookup.cell_size, inset);
}

Vector3 ProcCityGenerator::find_clear_point(const Vector3 &p_local_point, double p_max_radius) const {
	const CellLookup lookup = unpack_cell_heights(get_cell_heights());
	if (!lookup.is_valid()) {
		return p_local_point;
	}
	const float clip = MAX(0.0f, (float)clip_below_height);
	const Vector2i start = lookup.cell_at((float)p_local_point.x, (float)p_local_point.z);
	if (start.x < 0) {
		return p_local_point;
	}
	const float step = MAX(lookup.cell_size.x, lookup.cell_size.y);
	const int max_rings = step > 0.0f ? (int)Math::ceil(MAX(0.0, p_max_radius) / step) : 0;
	const Vector2i clear_cell = find_nearest_clear_cell(lookup, start, clip, max_rings);
	if (clear_cell.x < 0) {
		return p_local_point;
	}
	return Vector3(
			lookup.origin.x + ((float)clear_cell.x + 0.5f) * lookup.cell_size.x,
			p_local_point.y,
			lookup.origin.y + ((float)clear_cell.y + 0.5f) * lookup.cell_size.y);
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
