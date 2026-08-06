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

// Ground plane spanning the whole grid at y=0, closing the view through the
// streets that open up once blocks go freestanding.
void emit_ground(PackedSurface::Writer &p_writer, const BlockUvMap &p_uv_map, const CellGrid &p_grid, float p_y) {
	const float x1 = p_grid.ox + p_grid.cw * (float)p_grid.cols;
	const float z1 = p_grid.oz + p_grid.cd * (float)p_grid.rows;
	const Vector3 pos[4] = {
		Vector3(p_grid.ox, p_y, p_grid.oz), Vector3(x1, p_y, p_grid.oz),
		Vector3(x1, p_y, z1), Vector3(p_grid.ox, p_y, z1)
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

Ref<ArrayMesh> HeightmapMesher::build_array_mesh_with_heights(const Ref<Image> &p_image, const Vector2 &p_size,
			const Vector2i &p_verts, double p_height_scale, double p_base_height, int p_filter,
			 double p_height_power, double p_inset, int64_t p_seed, double p_ao, double p_variation,
			 double p_clip_below_height, std::vector<float> &r_heights) const {
	const CellGrid grid = make_cell_grid(p_size, p_verts);
	if (!resolve_cell_heights(p_image, grid, p_height_scale, p_base_height, p_filter, r_heights, p_height_power)) {
		return Ref<ArrayMesh>();
	}

	const float inset = CLAMP((float)p_inset, 0.0f, MAX_BLOCK_INSET);
	const bool freestanding = inset > 0.0f;
	const float clip_below = MAX(0.0f, (float)p_clip_below_height);

	const BlockStyle style = make_block_style(p_ao, p_variation, p_height_scale, p_seed);
	const BlockUvMap uv_map(p_size, grid);
	const std::vector<int64_t> offsets = row_quad_offsets(r_heights, grid, freestanding, clip_below);
	const int64_t block_quads = offsets[(size_t)grid.rows];
	const int64_t total_quads = block_quads + (freestanding ? 1 : 0);
	if (total_quads == 0) {
		Ref<ArrayMesh> empty_mesh;
		empty_mesh.instantiate();
		return empty_mesh;
	}

	PackedSurface surface(total_quads * QUAD_VERTS, total_quads * QUAD_INDICES, style.wants_color());
	parallel_for_rows(grid.rows, [&](int p_begin, int p_end) {
		PackedSurface::Writer writer = surface.writer_at({ offsets[(size_t)p_begin] * QUAD_VERTS, offsets[(size_t)p_begin] * QUAD_INDICES });
		for (int j = p_begin; j < p_end; j++) {
			for (int i = 0; i < grid.cols; i++) {
				emit_cell(writer, uv_map, r_heights, grid, style, i, j, inset, freestanding, clip_below);
			}
		}
	});
	if (freestanding) {
		PackedSurface::Writer ground_writer = surface.writer_at({ block_quads * QUAD_VERTS, block_quads * QUAD_INDICES });
		emit_ground(ground_writer, uv_map, grid, clip_below);
	}
	return surface.commit();
}
