#include "meshing/gridmap_plan.h"

#include "meshing/height_sampling.h"

#include <godot_cpp/core/math.hpp>

using namespace godot;

// auto_level_height keeps cells as close to cubic as the footprint allows: the
// shorter horizontal side, so no cell ends up taller than it is wide.
static float auto_level_height(const CellGrid &p_grid) {
	return MIN(p_grid.cw, p_grid.cd);
}

// levels_for_height quantizes one column to the nearest whole number of levels,
// but never to zero: the ground layer stays solid instead of developing holes
// wherever the sampled surface dips below half a level. A level height small
// enough to overflow the conversion saturates at the cell cap instead, so the
// budget check below reports it rather than the cast going undefined.
static int levels_for_height(float p_height, float p_level_height) {
	const double levels = Math::round((double)p_height / (double)p_level_height);
	return (int)CLAMP(levels, 1.0, (double)GRIDMAP_MAX_CELLS);
}

// measure_columns quantizes every column and totals the plan before a single
// cell is placed: a bad level height can ask for orders of magnitude more cells
// than the column count hints at, and that has to be caught before the reserve.
static void measure_columns(const std::vector<float> &p_heights, float p_level_height, bool p_fill_columns,
							std::vector<int> &r_levels, int64_t &r_total, int &r_tallest) {
	r_levels.resize(p_heights.size());
	r_total = 0;
	r_tallest = 0;
	for (size_t c = 0; c < p_heights.size(); c++) {
		const int levels = levels_for_height(p_heights[c], p_level_height);
		r_levels[c] = levels;
		r_total += p_fill_columns ? (int64_t)levels : 1;
		r_tallest = MAX(r_tallest, levels);
	}
}

bool godot::build_gridmap_plan(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
							   double p_height_scale, double p_base_height, int p_filter, double p_height_power,
							   double p_level_height, bool p_fill_columns, GridMapPlan &r_plan, String &r_error) {
	const CellGrid grid = make_cell_grid(p_size, p_verts);

	float level_height = (float)p_level_height;
	if (level_height <= 0.0f) {
		level_height = auto_level_height(grid);
	}
	if (level_height <= 0.0f) {
		r_error = "GridMap cell size resolved to zero; check Mesh Size and Grid Vertices.";
		return false;
	}

	std::vector<float> heights;
	if (!resolve_cell_heights(p_image, grid, p_height_scale, p_base_height, p_filter, heights, p_height_power)) {
		r_error = "Height image could not be sampled.";
		return false;
	}

	std::vector<int> column_levels;
	int64_t total = 0;
	int tallest = 0;
	measure_columns(heights, level_height, p_fill_columns, column_levels, total, tallest);
	if (total > GRIDMAP_MAX_CELLS) {
		r_error = String("GridMap plan needs ") + String::num_int64(total) + String(" cells, over the ") +
				  String::num_int64(GRIDMAP_MAX_CELLS) + String(" limit. Raise GridMap Level Height, lower Grid Vertices, or turn off GridMap Fill Columns.");
		return false;
	}

	r_plan.cell_size = Vector3(grid.cw, level_height, grid.cd);
	r_plan.origin = Vector3(grid.ox, 0.0f, grid.oz);
	r_plan.columns = grid.cols;
	r_plan.rows = grid.rows;
	r_plan.levels = tallest;
	r_plan.cells.clear();
	r_plan.cells.reserve((size_t)total);
	for (int j = 0; j < grid.rows; j++) {
		for (int i = 0; i < grid.cols; i++) {
			const int levels = column_levels[(size_t)j * (size_t)grid.cols + (size_t)i];
			if (!p_fill_columns) {
				r_plan.cells.push_back(Vector3i(i, levels - 1, j));
				continue;
			}
			for (int k = 0; k < levels; k++) {
				r_plan.cells.push_back(Vector3i(i, k, j));
			}
		}
	}
	return true;
}
