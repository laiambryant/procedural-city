#ifndef GRIDMAP_PLAN_H
#define GRIDMAP_PLAN_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <vector>

namespace godot {

// A GridMap holds far more cells than the column budget suggests: every column
// contributes one cell per vertical level. Past the warning threshold the
// editor still copes but octant rebuilds get sluggish; past the hard cap the
// plan is refused rather than left to exhaust memory.
inline constexpr int64_t GRIDMAP_CELL_WARNING_THRESHOLD = 65536;
inline constexpr int64_t GRIDMAP_MAX_CELLS = 1048576;

// GridMapPlan is the voxel layout of one city: columns x rows stacks of cells,
// each cell_size metres, plus the coordinates of every occupied cell.
struct GridMapPlan {
	Vector3 cell_size = Vector3(1, 1, 1);
	// Translation that lines cell (0, 0, 0) up with the north-west block of the
	// mesh-space grid, for a GridMap on its default cell_center_* flags (which
	// already centre each cell on its own coordinate). Level 0 sits on y = 0,
	// the same ground plane the mesh backends extrude from.
	Vector3 origin;
	std::vector<Vector3i> cells;
	int columns = 0;
	int rows = 0;
	// Tallest column, in cells.
	int levels = 0;
};

// build_gridmap_plan quantizes the sampled block heights into stacked cells.
// A level_height <= 0 picks the level height automatically, so cells come out
// as close to cubic as the footprint allows. fill_columns stacks every cell
// from the ground up; when false only each column's topmost cell is placed (a
// hollow surface: far fewer cells, nothing inside). Returns false and fills
// r_error when the height image is unusable or the plan would exceed
// GRIDMAP_MAX_CELLS.
bool build_gridmap_plan(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
		double p_height_scale, double p_base_height, int p_filter, double p_height_power,
		double p_level_height, bool p_fill_columns, GridMapPlan &r_plan, String &r_error);

} // namespace godot

#endif // GRIDMAP_PLAN_H
