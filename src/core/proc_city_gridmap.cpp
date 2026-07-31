#include "core/proc_city_gridmap.h"

#include "core/proc_city_generator.h"
#include "meshing/heightmap_mesher.h"

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/primitive_mesh.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// block_footprint shrinks the block inside its cell by p_inset per side, as in
// the other backends, while the cell size itself stays on the grid — so the
// inset reads as streets between freestanding buildings.
static Vector3 block_footprint(const Vector3 &p_cell_size, double p_inset) {
	const float shrink = 1.0f - 2.0f * CLAMP((float)p_inset, 0.0f, MAX_BLOCK_INSET);
	return Vector3(p_cell_size.x * shrink, p_cell_size.y, p_cell_size.z * shrink);
}

// set_block_shape gives the item the box shape a GridMap builds its static
// bodies from. MeshLibrary takes each shape and its transform as a flat pair.
static void set_block_shape(const Ref<MeshLibrary> &p_library, const Vector3 &p_block_size) {
	Ref<BoxShape3D> shape;
	shape.instantiate();
	shape->set_size(p_block_size);
	Array shapes;
	shapes.push_back(shape);
	shapes.push_back(Transform3D());
	p_library->set_item_shapes(GRIDMAP_BLOCK_ITEM, shapes);
}

Ref<MeshLibrary> godot::build_block_library(const Vector3 &p_cell_size, double p_inset, bool p_with_collision) {
	const Vector3 block_size = block_footprint(p_cell_size, p_inset);

	Ref<BoxMesh> box;
	box.instantiate();
	box->set_size(block_size);

	Ref<MeshLibrary> library;
	library.instantiate();
	library->create_item(GRIDMAP_BLOCK_ITEM);
	library->set_item_name(GRIDMAP_BLOCK_ITEM, "Block");
	library->set_item_mesh(GRIDMAP_BLOCK_ITEM, box);
	if (p_with_collision) {
		set_block_shape(library, block_size);
	}
	return library;
}

// centre_cells pins the cell_center_* flags a GridMapPlan's origin is computed
// against. They are GridMap's own defaults; setting them explicitly keeps the
// alignment from drifting with the engine's.
static void centre_cells(GridMap *p_gridmap) {
	p_gridmap->set_center_x(true);
	p_gridmap->set_center_y(true);
	p_gridmap->set_center_z(true);
}

GridMap *godot::build_gridmap_node(const GridMapPlan &p_plan, const Ref<MeshLibrary> &p_library, int p_item) {
	GridMap *gridmap = memnew(GridMap);
	centre_cells(gridmap);
	gridmap->set_cell_size(p_plan.cell_size);
	gridmap->set_mesh_library(p_library);
	for (const Vector3i &cell : p_plan.cells) {
		gridmap->set_cell_item(cell, p_item);
	}
	gridmap->set_position(p_plan.origin);
	return gridmap;
}

void godot::apply_library_material(const Ref<MeshLibrary> &p_library, const Ref<Material> &p_material) {
	const PackedInt32Array items = p_library->get_item_list();
	for (int i = 0; i < items.size(); i++) {
		Ref<PrimitiveMesh> mesh = p_library->get_item_mesh(items[i]);
		if (mesh.is_valid()) {
			mesh->set_material(p_material);
		}
	}
}

// A plan past the warning threshold still builds, but GridMap's octant rebuilds
// get slow enough to be worth flagging before the editor goes quiet.
static void warn_on_gridmap_size(const GridMapPlan &p_plan) {
	const int64_t cell_count = (int64_t)p_plan.cells.size();
	if (cell_count > GRIDMAP_CELL_WARNING_THRESHOLD) {
		UtilityFunctions::push_warning(String("[ProcCity] Placing ") + String::num_int64(cell_count) + String(" GridMap cells; octant rebuilds may stall the editor."));
	}
}

// _resolve_gridmap_library picks the library the cells will reference: the
// user's when they supplied one — used untouched, so it is not tracked for
// retinting — otherwise a freshly built block library.
bool ProcCityGenerator::_resolve_gridmap_library(const Vector3 &p_cell_size, Ref<MeshLibrary> &r_library, int &r_item) {
	if (gridmap_mesh_library.is_null()) {
		r_library = build_block_library(p_cell_size, block_inset, generate_collision);
		r_item = GRIDMAP_BLOCK_ITEM;
		_generated_library = r_library;
		return true;
	}
	if (!gridmap_mesh_library->get_item_list().has(gridmap_item_id)) {
		_emit_failed("geometry", String("GridMap Mesh Library has no item ") + String::num_int64(gridmap_item_id) + String("."));
		return false;
	}
	r_library = gridmap_mesh_library;
	r_item = gridmap_item_id;
	_generated_library = Ref<MeshLibrary>();
	return true;
}

Node3D *ProcCityGenerator::_make_gridmap_node() {
	GridMapPlan plan;
	String error;
	if (!build_gridmap_plan(_height_image, mesh_size, grid_vertices, height_scale, base_height, sample_filter,
							height_power, gridmap_level_height, gridmap_fill_columns, plan, error)) {
		_emit_failed("geometry", error);
		return nullptr;
	}
	warn_on_gridmap_size(plan);

	Ref<MeshLibrary> library;
	int item = GRIDMAP_BLOCK_ITEM;
	if (!_resolve_gridmap_library(plan.cell_size, library, item)) {
		return nullptr;
	}
	return build_gridmap_node(plan, library, item);
}
