#include "meshing/heightmap_mesher.h"

#include "meshing/baked_style.h"
#include "meshing/deterministic_noise.h"
#include "meshing/height_sampling.h"
#include "meshing/packed_surface.h"
#include "meshing/parallel_rows.h"

#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

using namespace godot;

namespace {

struct BlockStyle {
	float ao = 0.0f;
	float variation = 0.0f;
	float href = 1.0f;
	uint32_t seed = 0;

	bool wants_color() const { return ao > 0.0f || variation > 0.0f; }
};

BlockStyle make_block_style(double p_ao, double p_variation, double p_height_scale, int64_t p_seed) {
	BlockStyle style;
	style.ao = CLAMP((float)p_ao, 0.0f, 1.0f);
	style.variation = CLAMP((float)p_variation, 0.0f, 1.0f);
	style.href = MAX(MIN_AO_HEIGHT_REFERENCE, (float)p_height_scale);
	style.seed = (uint32_t)((uint64_t)p_seed ^ ((uint64_t)p_seed >> 32));
	return style;
}

float height_at(const std::vector<float> &p_heights, const CellGrid &p_grid, int p_i, int p_j) {
	return p_heights[(size_t)p_j * (size_t)p_grid.cols + (size_t)p_i];
}

float neighbour_height_or_floor(const std::vector<float> &p_heights, const CellGrid &p_grid, int p_i, int p_j) {
	if (p_i < 0 || p_i >= p_grid.cols || p_j < 0 || p_j >= p_grid.rows) {
		return 0.0f;
	}
	return height_at(p_heights, p_grid, p_i, p_j);
}

Color grey(float p_v) {
	return Color(p_v, p_v, p_v);
}

// BlockShader resolves the baked colours for one cell: its tint, the roof
// corner occlusion cast by taller neighbours, and the wall gradient that
// darkens toward canyon floors.
struct BlockShader {
	const std::vector<float> &heights;
	const CellGrid &grid;
	const BlockStyle &style;
	int i = 0;
	int j = 0;
	float top = 0.0f;
	float tint = 1.0f;

	BlockShader(const std::vector<float> &p_heights, const CellGrid &p_grid, const BlockStyle &p_style,
				int p_i, int p_j, float p_top) :
			heights(p_heights), grid(p_grid), style(p_style), i(p_i), j(p_j), top(p_top) {
		tint = 1.0f - style.variation * VARIATION_TINT_SPAN * hash01(p_i, p_j, style.seed ^ SALT_CELL_TINT);
	}

	Color roof_corner(int p_di, int p_dj) const {
		float occ = 0.0f;
		const int ni = i + (p_di ? 1 : -1);
		const int nj = j + (p_dj ? 1 : -1);
		occ = MAX(occ, neighbour_height_or_floor(heights, grid, ni, j) - top);
		occ = MAX(occ, neighbour_height_or_floor(heights, grid, i, nj) - top);
		occ = MAX(occ, neighbour_height_or_floor(heights, grid, ni, nj) - top);
		const float t = CLAMP(occ / style.href, 0.0f, 1.0f);
		return grey(tint * (1.0f - style.ao * ROOF_AO_SPAN * t));
	}

	Color wall_bottom(float p_bottom_y) const {
		const float depth = CLAMP((top - p_bottom_y) / style.href, 0.0f, 1.0f);
		return grey(tint * (1.0f - style.ao * WALL_AO_SPAN * depth));
	}

	Color wall_top() const { return grey(tint); }
};

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

void emit_roof(PackedSurface::Writer &p_writer, const BlockUvMap &p_uv_map, const BlockShader &p_shader,
			   const CellBounds &p_b, float p_top) {
	const Vector3 pos[4] = {
		Vector3(p_b.x0, p_top, p_b.z0), Vector3(p_b.x1, p_top, p_b.z0),
		Vector3(p_b.x1, p_top, p_b.z1), Vector3(p_b.x0, p_top, p_b.z1)
	};
	const Vector2 uv[4] = {
		p_uv_map.planar_uv(p_b.x0, p_b.z0), p_uv_map.planar_uv(p_b.x1, p_b.z0),
		p_uv_map.planar_uv(p_b.x1, p_b.z1), p_uv_map.planar_uv(p_b.x0, p_b.z1)
	};
	const Color col[4] = {
		p_shader.roof_corner(0, 0), p_shader.roof_corner(1, 0),
		p_shader.roof_corner(1, 1), p_shader.roof_corner(0, 1)
	};
	p_writer.quad(pos, uv, col, Vector3(0, 1, 0), Vector3(1, 0, 0), TANGENT_W);
}

void emit_wall(PackedSurface::Writer &p_writer, const BlockUvMap &p_uv_map, const BlockShader &p_shader,
			   const WallFace &p_face, float p_top) {
	const Vector3 pos[4] = {
		p_face.bottom_a, p_face.bottom_b,
		Vector3(p_face.bottom_b.x, p_top, p_face.bottom_b.z), Vector3(p_face.bottom_a.x, p_top, p_face.bottom_a.z)
	};
	const Vector2 uv[4] = {
		p_uv_map.drape_uv(pos[0], p_face.normal, p_top), p_uv_map.drape_uv(pos[1], p_face.normal, p_top),
		p_uv_map.drape_uv(pos[2], p_face.normal, p_top), p_uv_map.drape_uv(pos[3], p_face.normal, p_top)
	};
	const Color bottom = p_shader.wall_bottom(p_face.bottom_a.y);
	const Color top = p_shader.wall_top();
	const Color col[4] = { bottom, bottom, top, top };
	p_writer.quad(pos, uv, col, p_face.normal, wall_tangent(p_face.normal), TANGENT_W);
}

void emit_cell(PackedSurface::Writer &p_writer, const BlockUvMap &p_uv_map,
			   const std::vector<float> &p_heights, const CellGrid &p_grid, const BlockStyle &p_style,
			   int p_i, int p_j, float p_inset, bool p_freestanding) {
	const float top = height_at(p_heights, p_grid, p_i, p_j);
	const CellBounds b(p_grid, p_i, p_j, p_inset);
	const BlockShader shader(p_heights, p_grid, p_style, p_i, p_j, top);
	emit_roof(p_writer, p_uv_map, shader, b, top);
	WallFace faces[4];
	const int walls = visible_walls(p_heights, p_grid, p_i, p_j, top, b, p_freestanding, faces);
	for (int w = 0; w < walls; w++) {
		emit_wall(p_writer, p_uv_map, shader, faces[w], top);
	}
}

// Ground plane spanning the whole grid at y=0, closing the view through the
// streets that open up once blocks go freestanding.
void emit_ground(PackedSurface::Writer &p_writer, const BlockUvMap &p_uv_map, const CellGrid &p_grid) {
	const float x1 = p_grid.ox + p_grid.cw * (float)p_grid.cols;
	const float z1 = p_grid.oz + p_grid.cd * (float)p_grid.rows;
	const Vector3 pos[4] = {
		Vector3(p_grid.ox, 0.0f, p_grid.oz), Vector3(x1, 0.0f, p_grid.oz),
		Vector3(x1, 0.0f, z1), Vector3(p_grid.ox, 0.0f, z1)
	};
	const Vector2 uv[4] = {
		p_uv_map.planar_uv(pos[0].x, pos[0].z), p_uv_map.planar_uv(pos[1].x, pos[1].z),
		p_uv_map.planar_uv(pos[2].x, pos[2].z), p_uv_map.planar_uv(pos[3].x, pos[3].z)
	};
	const Color white(1, 1, 1);
	const Color col[4] = { white, white, white, white };
	p_writer.quad(pos, uv, col, Vector3(0, 1, 0), Vector3(1, 0, 0), TANGENT_W);
}

} // namespace

Ref<ArrayMesh> HeightmapMesher::build_array_mesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
												 double p_height_scale, double p_base_height, int p_filter,
												 double p_height_power, double p_inset, int64_t p_seed, double p_ao, double p_variation) const {
	const CellGrid grid = make_cell_grid(p_size, p_verts);
	std::vector<float> heights;
	if (!resolve_cell_heights(p_image, grid, p_height_scale, p_base_height, p_filter, heights, p_height_power)) {
		return Ref<ArrayMesh>();
	}

	const float inset = CLAMP((float)p_inset, 0.0f, MAX_BLOCK_INSET);
	const bool freestanding = inset > 0.0f;

	const BlockStyle style = make_block_style(p_ao, p_variation, p_height_scale, p_seed);
	const BlockUvMap uv_map(p_size, grid);
	const std::vector<int64_t> offsets = row_quad_offsets(heights, grid, inset, freestanding);
	const int64_t block_quads = offsets[(size_t)grid.rows];
	const int64_t total_quads = block_quads + (freestanding ? 1 : 0);

	PackedSurface surface(total_quads * QUAD_VERTS, total_quads * QUAD_INDICES, style.wants_color());
	parallel_for_rows(grid.rows, [&](int p_begin, int p_end) {
		PackedSurface::Writer writer = surface.writer_at({ offsets[(size_t)p_begin] * QUAD_VERTS, offsets[(size_t)p_begin] * QUAD_INDICES });
		for (int j = p_begin; j < p_end; j++) {
			for (int i = 0; i < grid.cols; i++) {
				emit_cell(writer, uv_map, heights, grid, style, i, j, inset, freestanding);
			}
		}
	});
	if (freestanding) {
		PackedSurface::Writer ground_writer = surface.writer_at({ block_quads * QUAD_VERTS, block_quads * QUAD_INDICES });
		emit_ground(ground_writer, uv_map, grid);
	}
	return surface.commit();
}
