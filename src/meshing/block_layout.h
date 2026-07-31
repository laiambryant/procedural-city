#ifndef BLOCK_LAYOUT_H
#define BLOCK_LAYOUT_H

#include "meshing/height_sampling.h"
#include "meshing/packed_surface.h"
#include "meshing/parallel_rows.h"

#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

namespace godot {

// Where the block backend's geometry goes before anything is written: the UV
// mapping, one cell's footprint, which of its walls are visible, and the
// per-row prefix sums that give each thread a private slice of the output.
// Header-only for the same reason as block_style.h — this is per-cell code.

float height_at(const std::vector<float> &p_heights, const CellGrid &p_grid, int p_i, int p_j) {
	return p_heights[(size_t)p_j * (size_t)p_grid.cols + (size_t)p_i];
}

float neighbour_height_or_floor(const std::vector<float> &p_heights, const CellGrid &p_grid, int p_i, int p_j) {
	if (p_i < 0 || p_i >= p_grid.cols || p_j < 0 || p_j >= p_grid.rows) {
		return 0.0f;
	}
	return height_at(p_heights, p_grid, p_i, p_j);
}

struct BlockUvMap {
	float sx = 1.0f;
	float sz = 1.0f;
	float ox = 0.0f;
	float oz = 0.0f;

	BlockUvMap(const Vector2 &p_size, const CellGrid &p_grid) :
			sx(p_size.x != 0.0f ? p_size.x : 1.0f),
			sz(p_size.y != 0.0f ? p_size.y : 1.0f),
			ox(p_grid.ox),
			oz(p_grid.oz) {}

	Vector2 planar_uv(float p_x, float p_z) const {
		return Vector2((p_x - ox) / sx, (p_z - oz) / sz);
	}

	// Wall UV: the roof's planar mapping slid down the face by the drop below
	// the roofline, in the direction the wall faces.
	Vector2 drape_uv(const Vector3 &p_pos, const Vector3 &p_normal, float p_eave_y) const {
		const float drop = p_eave_y - p_pos.y;
		return planar_uv(p_pos.x, p_pos.z) + Vector2(p_normal.x / sx, p_normal.z / sz) * drop;
	}
};

// Analytic mikktspace equivalents for the draped mapping: every face carries
// w = -1, walls facing +-X have their tangent along +-Y because there the UV's
// u coordinate only varies with height.
Vector3 wall_tangent(const Vector3 &p_normal) {
	if (p_normal.x != 0.0f) {
		return Vector3(0.0f, -p_normal.x, 0.0f);
	}
	return Vector3(1.0f, 0.0f, 0.0f);
}

constexpr float TANGENT_W = -1.0f;

// Every face is one quad: 4 unique vertices, 2 indexed triangles.
constexpr int64_t QUAD_VERTS = 4;
constexpr int64_t QUAD_INDICES = 6;

struct CellBounds {
	float x0, x1, z0, z1;

	// p_inset (fraction of a cell per side) shrinks the footprint, used for
	// freestanding blocks with streets between them.
	CellBounds(const CellGrid &p_grid, int p_i, int p_j, float p_inset = 0.0f) :
			x0(p_grid.ox + (float)p_i * p_grid.cw + p_grid.cw * p_inset),
			x1(p_grid.ox + (float)(p_i + 1) * p_grid.cw - p_grid.cw * p_inset),
			z0(p_grid.oz + (float)p_j * p_grid.cd + p_grid.cd * p_inset),
			z1(p_grid.oz + (float)(p_j + 1) * p_grid.cd - p_grid.cd * p_inset) {}
};

struct WallFace {
	Vector3 bottom_a;
	Vector3 bottom_b;
	Vector3 normal;
};

// Legacy face order per cell: roof, then south/north/west/east walls. Merged
// mode emits a wall only where the block rises above that neighbour (shared
// interior walls stay culled); freestanding mode (inset > 0) always emits all
// four, dropping to the floor, since every block stands alone over streets.
int visible_walls(const std::vector<float> &p_heights, const CellGrid &p_grid,
				  int p_i, int p_j, float p_top, const CellBounds &p_b, bool p_freestanding, WallFace r_faces[4]) {
	int count = 0;
	const float south = p_freestanding ? 0.0f : neighbour_height_or_floor(p_heights, p_grid, p_i, p_j - 1);
	if (p_freestanding || p_top > south) {
		r_faces[count++] = { Vector3(p_b.x0, south, p_b.z0), Vector3(p_b.x1, south, p_b.z0), Vector3(0, 0, -1) };
	}
	const float north = p_freestanding ? 0.0f : neighbour_height_or_floor(p_heights, p_grid, p_i, p_j + 1);
	if (p_freestanding || p_top > north) {
		r_faces[count++] = { Vector3(p_b.x1, north, p_b.z1), Vector3(p_b.x0, north, p_b.z1), Vector3(0, 0, 1) };
	}
	const float west = p_freestanding ? 0.0f : neighbour_height_or_floor(p_heights, p_grid, p_i - 1, p_j);
	if (p_freestanding || p_top > west) {
		r_faces[count++] = { Vector3(p_b.x0, west, p_b.z1), Vector3(p_b.x0, west, p_b.z0), Vector3(-1, 0, 0) };
	}
	const float east = p_freestanding ? 0.0f : neighbour_height_or_floor(p_heights, p_grid, p_i + 1, p_j);
	if (p_freestanding || p_top > east) {
		r_faces[count++] = { Vector3(p_b.x1, east, p_b.z0), Vector3(p_b.x1, east, p_b.z1), Vector3(1, 0, 0) };
	}
	return count;
}

int cell_quad_count(const std::vector<float> &p_heights, const CellGrid &p_grid, int p_i, int p_j,
					float p_inset, bool p_freestanding) {
	const float top = height_at(p_heights, p_grid, p_i, p_j);
	const CellBounds b(p_grid, p_i, p_j, p_inset);
	WallFace faces[4];
	return 1 + visible_walls(p_heights, p_grid, p_i, p_j, top, b, p_freestanding, faces);
}

// Quad layout per row is fixed by the emission order, so per-row prefix sums
// give every band a private [vertex, index) range.
std::vector<int64_t> row_quad_offsets(const std::vector<float> &p_heights, const CellGrid &p_grid,
									  float p_inset, bool p_freestanding) {
	std::vector<int64_t> row_quads((size_t)p_grid.rows, 0);
	parallel_for_rows(p_grid.rows, [&](int p_begin, int p_end) {
		for (int j = p_begin; j < p_end; j++) {
			int64_t quads = 0;
			for (int i = 0; i < p_grid.cols; i++) {
				quads += cell_quad_count(p_heights, p_grid, i, j, p_inset, p_freestanding);
			}
			row_quads[(size_t)j] = quads;
		}
	});

	std::vector<int64_t> offsets((size_t)p_grid.rows + 1, 0);
	for (int j = 0; j < p_grid.rows; j++) {
		offsets[(size_t)j + 1] = offsets[(size_t)j] + row_quads[(size_t)j];
	}
	return offsets;
}

} // namespace godot

#endif // BLOCK_LAYOUT_H
