#ifndef BLOCK_LAYOUT_H
#define BLOCK_LAYOUT_H

#include "meshing/height_sampling.h"
#include "meshing/packed_surface.h"
#include "meshing/parallel_rows.h"

#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

namespace godot {

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

	Vector2 drape_uv(const Vector3 &p_pos, const Vector3 &p_normal, float p_eave_y) const {
		const float drop = p_eave_y - p_pos.y;
		return planar_uv(p_pos.x, p_pos.z) + Vector2(p_normal.x / sx, p_normal.z / sz) * drop;
	}
};

Vector3 wall_tangent(const Vector3 &p_normal) {
	const bool wall_faces_x_axis = p_normal.x != 0.0f;
	if (wall_faces_x_axis) {
		return Vector3(0.0f, -p_normal.x, 0.0f);
	}
	return Vector3(1.0f, 0.0f, 0.0f);
}

constexpr float TANGENT_W = -1.0f;

constexpr int64_t QUAD_VERTS = 4;
constexpr int64_t QUAD_INDICES = 6;

struct CellBounds {
	float x0, x1, z0, z1;

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

int visible_walls(const std::vector<float> &p_heights, const CellGrid &p_grid,
		int p_i, int p_j, float p_top, const CellBounds &p_b, bool p_freestanding,
		float p_clip_below_height, WallFace r_faces[4]) {
	int count = 0;
	const float floor = MAX(0.0f, p_clip_below_height);
	const float south = p_freestanding ? floor : MAX(floor, neighbour_height_or_floor(p_heights, p_grid, p_i, p_j - 1));
	if (p_freestanding || p_top > south) {
		r_faces[count++] = { Vector3(p_b.x0, south, p_b.z0), Vector3(p_b.x1, south, p_b.z0), Vector3(0, 0, -1) };
	}
	const float north = p_freestanding ? floor : MAX(floor, neighbour_height_or_floor(p_heights, p_grid, p_i, p_j + 1));
	if (p_freestanding || p_top > north) {
		r_faces[count++] = { Vector3(p_b.x1, north, p_b.z1), Vector3(p_b.x0, north, p_b.z1), Vector3(0, 0, 1) };
	}
	const float west = p_freestanding ? floor : MAX(floor, neighbour_height_or_floor(p_heights, p_grid, p_i - 1, p_j));
	if (p_freestanding || p_top > west) {
		r_faces[count++] = { Vector3(p_b.x0, west, p_b.z1), Vector3(p_b.x0, west, p_b.z0), Vector3(-1, 0, 0) };
	}
	const float east = p_freestanding ? floor : MAX(floor, neighbour_height_or_floor(p_heights, p_grid, p_i + 1, p_j));
	if (p_freestanding || p_top > east) {
		r_faces[count++] = { Vector3(p_b.x1, east, p_b.z0), Vector3(p_b.x1, east, p_b.z1), Vector3(1, 0, 0) };
	}
	return count;
}

int cell_quad_count(const std::vector<float> &p_heights, const CellGrid &p_grid, int p_i, int p_j,
		bool p_freestanding, float p_clip_below_height) {
	const float top = height_at(p_heights, p_grid, p_i, p_j);
	const float floor = MAX(0.0f, p_clip_below_height);
	if (top <= floor) {
		return 0;
	}
	if (p_freestanding) {
		return 5;
	}
	int quads = 1;
	quads += top > MAX(floor, neighbour_height_or_floor(p_heights, p_grid, p_i, p_j - 1));
	quads += top > MAX(floor, neighbour_height_or_floor(p_heights, p_grid, p_i, p_j + 1));
	quads += top > MAX(floor, neighbour_height_or_floor(p_heights, p_grid, p_i - 1, p_j));
	quads += top > MAX(floor, neighbour_height_or_floor(p_heights, p_grid, p_i + 1, p_j));
	return quads;
}

struct CellRect {
	int i0 = 0;
	int i1 = 0;
	int j0 = 0;
	int j1 = 0;

	int cols() const { return i1 - i0; }
	int rows() const { return j1 - j0; }
	bool is_empty() const { return cols() <= 0 || rows() <= 0; }

	static CellRect whole(const CellGrid &p_grid) { return { 0, p_grid.cols, 0, p_grid.rows }; }
};

std::vector<int64_t> row_quad_offsets(const std::vector<float> &p_heights, const CellGrid &p_grid,
		bool p_freestanding, float p_clip_below_height, const CellRect &p_rect) {
	const int rows = p_rect.rows();
	std::vector<int64_t> row_quads((size_t)rows, 0);
	parallel_for_rows(rows, [&](int p_begin, int p_end) {
		for (int r = p_begin; r < p_end; r++) {
			const int j = p_rect.j0 + r;
			int64_t quads = 0;
			for (int i = p_rect.i0; i < p_rect.i1; i++) {
				quads += cell_quad_count(p_heights, p_grid, i, j, p_freestanding, p_clip_below_height);
			}
			row_quads[(size_t)r] = quads;
		}
	});

	std::vector<int64_t> offsets((size_t)rows + 1, 0);
	for (int r = 0; r < rows; r++) {
		offsets[(size_t)r + 1] = offsets[(size_t)r] + row_quads[(size_t)r];
	}
	return offsets;
}

} // namespace godot

#endif // BLOCK_LAYOUT_H
