#include "meshing/heightmap_mesher.h"

#include "meshing/block_layout.h"
#include "meshing/block_style.h"
#include "meshing/height_sampling.h"
#include "meshing/packed_surface.h"
#include "meshing/parallel_rows.h"

#include <godot_cpp/variant/vector3.hpp>

#include <vector>

using namespace godot;

namespace {

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
		int p_i, int p_j, float p_inset, bool p_freestanding, float p_clip_below_height) {
	const float top = height_at(p_heights, p_grid, p_i, p_j);
	if (top <= p_clip_below_height) {
		return;
	}
	const CellBounds b(p_grid, p_i, p_j, p_inset);
	const BlockShader shader(p_heights, p_grid, p_style, p_i, p_j, top);
	emit_roof(p_writer, p_uv_map, shader, b, top);
	WallFace faces[4];
	const int walls = visible_walls(p_heights, p_grid, p_i, p_j, top, b, p_freestanding, p_clip_below_height, faces);
	for (int w = 0; w < walls; w++) {
		emit_wall(p_writer, p_uv_map, shader, faces[w], top);
	}
}

// Ground plane under one cell rect at y=0, closing the view through the streets
// that open up once blocks go freestanding. Chunked geometry gives each chunk
// its own patch so it culls with the chunk instead of keeping a grid-wide quad
// (and its whole AABB) permanently visible.
void emit_ground(PackedSurface::Writer &p_writer, const BlockUvMap &p_uv_map, const CellGrid &p_grid,
		const CellRect &p_rect, float p_y) {
	const float x0 = p_grid.ox + p_grid.cw * (float)p_rect.i0;
	const float x1 = p_grid.ox + p_grid.cw * (float)p_rect.i1;
	const float z0 = p_grid.oz + p_grid.cd * (float)p_rect.j0;
	const float z1 = p_grid.oz + p_grid.cd * (float)p_rect.j1;
	const Vector3 pos[4] = {
		Vector3(x0, p_y, z0), Vector3(x1, p_y, z0),
		Vector3(x1, p_y, z1), Vector3(x0, p_y, z1)
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
		double p_height_power, double p_inset, int64_t p_seed, double p_ao,
		double p_variation, double p_clip_below_height) const {
	std::vector<float> heights;
	return build_array_mesh_with_heights(p_image, p_size, p_verts, p_height_scale, p_base_height, p_filter,
			p_height_power, p_inset, p_seed, p_ao, p_variation, p_clip_below_height, heights);
}

// build_rect_mesh emits the cells of one rect into a fresh surface. Neighbour
// culling reads the whole height grid, so a mesh built per chunk is identical,
// triangle for triangle, to the corresponding slice of the single-mesh build.
static Ref<ArrayMesh> build_rect_mesh(const std::vector<float> &p_heights, const CellGrid &p_grid,
		const BlockStyle &p_style, const BlockUvMap &p_uv_map, const CellRect &p_rect,
		float p_inset, bool p_freestanding, float p_clip_below) {
	const std::vector<int64_t> offsets = row_quad_offsets(p_heights, p_grid, p_freestanding, p_clip_below, p_rect);
	const int64_t block_quads = offsets[(size_t)p_rect.rows()];
	const int64_t total_quads = block_quads + (p_freestanding ? 1 : 0);
	if (total_quads == 0) {
		Ref<ArrayMesh> empty_mesh;
		empty_mesh.instantiate();
		return empty_mesh;
	}

	PackedSurface surface(total_quads * QUAD_VERTS, total_quads * QUAD_INDICES, p_style.wants_color());
	parallel_for_rows(p_rect.rows(), [&](int p_begin, int p_end) {
		PackedSurface::Writer writer = surface.writer_at({ offsets[(size_t)p_begin] * QUAD_VERTS,
				offsets[(size_t)p_begin] * QUAD_INDICES });
		for (int r = p_begin; r < p_end; r++) {
			const int j = p_rect.j0 + r;
			for (int i = p_rect.i0; i < p_rect.i1; i++) {
				emit_cell(writer, p_uv_map, p_heights, p_grid, p_style, i, j, p_inset, p_freestanding, p_clip_below);
			}
		}
	});
	if (p_freestanding) {
		PackedSurface::Writer ground_writer = surface.writer_at({ block_quads * QUAD_VERTS, block_quads * QUAD_INDICES });
		emit_ground(ground_writer, p_uv_map, p_grid, p_rect, p_clip_below);
	}
	return surface.commit();
}

Ref<ArrayMesh> HeightmapMesher::build_array_mesh_with_heights(const Ref<Image> &p_image, const Vector2 &p_size,
		const Vector2i &p_verts, double p_height_scale, double p_base_height, int p_filter,
		double p_height_power, double p_inset, int64_t p_seed, double p_ao, double p_variation,
		double p_clip_below_height, std::vector<float> &r_heights) const {
	const CellGrid grid = make_cell_grid(p_size, p_verts);
	if (!resolve_cell_heights(p_image, grid, p_height_scale, p_base_height, p_filter, r_heights, p_height_power)) {
		return Ref<ArrayMesh>();
	}

	const float inset = CLAMP((float)p_inset, 0.0f, MAX_BLOCK_INSET);
	const float clip_below = MAX(0.0f, (float)p_clip_below_height);
	const BlockStyle style = make_block_style(p_ao, p_variation, p_height_scale, p_seed);
	const BlockUvMap uv_map(p_size, grid);
	return build_rect_mesh(r_heights, grid, style, uv_map, CellRect::whole(grid), inset, inset > 0.0f, clip_below);
}

// chunk_rect carves the grid into p_chunks x p_chunks tiles, giving the trailing
// tile whatever remainder is left so every cell lands in exactly one chunk.
static CellRect chunk_rect(const CellGrid &p_grid, int p_chunks, int p_cx, int p_cz) {
	const int step_i = (p_grid.cols + p_chunks - 1) / p_chunks;
	const int step_j = (p_grid.rows + p_chunks - 1) / p_chunks;
	CellRect rect;
	rect.i0 = MIN(p_cx * step_i, p_grid.cols);
	rect.i1 = MIN(rect.i0 + step_i, p_grid.cols);
	rect.j0 = MIN(p_cz * step_j, p_grid.rows);
	rect.j1 = MIN(rect.j0 + step_j, p_grid.rows);
	return rect;
}

std::vector<Ref<ArrayMesh>> HeightmapMesher::build_array_mesh_chunks(const Ref<Image> &p_image, const Vector2 &p_size,
		const Vector2i &p_verts, double p_height_scale, double p_base_height, int p_filter,
		double p_height_power, double p_inset, int64_t p_seed, double p_ao, double p_variation,
		double p_clip_below_height, int p_chunks, std::vector<float> &r_heights) const {
	std::vector<Ref<ArrayMesh>> meshes;
	const CellGrid grid = make_cell_grid(p_size, p_verts);
	if (!resolve_cell_heights(p_image, grid, p_height_scale, p_base_height, p_filter, r_heights, p_height_power)) {
		return meshes;
	}

	const int chunks = CLAMP(p_chunks, 1, MAX_GEOMETRY_CHUNKS);
	const float inset = CLAMP((float)p_inset, 0.0f, MAX_BLOCK_INSET);
	const bool freestanding = inset > 0.0f;
	const float clip_below = MAX(0.0f, (float)p_clip_below_height);
	const BlockStyle style = make_block_style(p_ao, p_variation, p_height_scale, p_seed);
	const BlockUvMap uv_map(p_size, grid);

	meshes.reserve((size_t)chunks * (size_t)chunks);
	for (int cz = 0; cz < chunks; cz++) {
		for (int cx = 0; cx < chunks; cx++) {
			const CellRect rect = chunk_rect(grid, chunks, cx, cz);
			if (rect.is_empty()) {
				continue;
			}
			meshes.push_back(build_rect_mesh(r_heights, grid, style, uv_map, rect, inset, freestanding, clip_below));
		}
	}
	return meshes;
}

TypedArray<ArrayMesh> HeightmapMesher::build_array_mesh_chunks_array(const Ref<Image> &p_image, const Vector2 &p_size,
		const Vector2i &p_verts, double p_height_scale, double p_base_height, int p_filter,
		double p_height_power, double p_inset, int64_t p_seed, double p_ao, double p_variation,
		double p_clip_below_height, int p_chunks) const {
	std::vector<float> heights;
	const std::vector<Ref<ArrayMesh>> meshes = build_array_mesh_chunks(p_image, p_size, p_verts, p_height_scale,
			p_base_height, p_filter, p_height_power, p_inset, p_seed, p_ao, p_variation, p_clip_below_height,
			p_chunks, heights);
	TypedArray<ArrayMesh> out;
	out.resize((int64_t)meshes.size());
	for (size_t i = 0; i < meshes.size(); i++) {
		out[(int64_t)i] = meshes[i];
	}
	return out;
}
