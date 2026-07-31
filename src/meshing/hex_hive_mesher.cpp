#include "meshing/heightmap_mesher.h"

#include "meshing/hex_lattice.h"
#include "meshing/packed_surface.h"
#include "meshing/parallel_rows.h"

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

namespace {

struct HexUvMap {
	const HexLayout &layout;

	Vector2 planar_uv(const Vector3 &p_pos) const {
		return Vector2((p_pos.x - layout.ox) / layout.sx, (p_pos.z - layout.oz) / layout.sz);
	}

	// Cap mapping slid down the wall by the drop below the rim, along the
	// outward normal, so vertical faces stop smearing a single texture row.
	Vector2 drape_uv(const Vector3 &p_pos, const Vector3 &p_n_out, float p_rim_y) const {
		const float drop = p_rim_y - p_pos.y;
		return planar_uv(p_pos) + Vector2(p_n_out.x / layout.sx, p_n_out.z / layout.sz) * drop;
	}
};

// Planar XZ UVs with N = +Y: u grows with +x, v with +z -> T = +X, w = -1.
void emit_hex_cap(PackedSurface::Writer &p_writer, const HexUvMap &p_uv_map,
				  const Vector3 p_corners[6], float p_tint) {
	Vector2 uv[6];
	for (int k = 0; k < 6; k++) {
		uv[k] = p_uv_map.planar_uv(p_corners[k]);
	}
	p_writer.fan(p_corners, uv, 6, Color(p_tint, p_tint, p_tint), Vector3(0, 1, 0), Vector3(1, 0, 0), -1.0f);
}

// Walls drop to the floor with no neighbour culling: the gap keeps every cell
// freestanding, so every wall is visible. UVs drape the cap's planar mapping
// down the face along the outward normal (seamless at the rim), and the base
// darkens toward the floor when AO is baked.
void emit_hex_walls(PackedSurface::Writer &p_writer, const HexUvMap &p_uv_map,
					const Vector3 p_corners[6], float p_tint_top, float p_tint_bottom) {
	const Color top_col(p_tint_top, p_tint_top, p_tint_top);
	const Color bot_col(p_tint_bottom, p_tint_bottom, p_tint_bottom);
	for (int k = 0; k < 6; k++) {
		const Vector3 &ca = p_corners[k];
		const Vector3 &cb = p_corners[(k + 1) % 6];
		const Vector3 edge = cb - ca;
		const Vector3 n_out = normalized_or_zero(Vector3(edge.z, 0.0f, -edge.x));
		const Vector3 t_wall = normalized_or_zero(Vector3(edge.x, 0.0f, edge.z));
		const Vector3 a_lo(ca.x, 0.0f, ca.z);
		const Vector3 b_lo(cb.x, 0.0f, cb.z);
		const Vector3 pos[4] = { a_lo, b_lo, cb, ca };
		const Vector2 uv[4] = {
			p_uv_map.drape_uv(a_lo, n_out, ca.y), p_uv_map.drape_uv(b_lo, n_out, cb.y),
			p_uv_map.planar_uv(cb), p_uv_map.planar_uv(ca)
		};
		const Color col[4] = { bot_col, bot_col, top_col, top_col };
		p_writer.quad(pos, uv, col, n_out, t_wall, 1.0f);
	}
}

// Ground plane at y=0 across the whole layout: closes the view through the
// inter-cell gaps and, darkened by AO, reads as alley floor.
void emit_hex_floor(PackedSurface::Writer &p_writer, const HexUvMap &p_uv_map,
					const HexLayout &p_layout, float p_shade) {
	const Vector3 pos[4] = {
		Vector3(p_layout.ox, 0.0f, p_layout.oz),
		Vector3(p_layout.ox + p_layout.sx, 0.0f, p_layout.oz),
		Vector3(p_layout.ox + p_layout.sx, 0.0f, p_layout.oz + p_layout.sz),
		Vector3(p_layout.ox, 0.0f, p_layout.oz + p_layout.sz)
	};
	const Vector2 uv[4] = {
		p_uv_map.planar_uv(pos[0]), p_uv_map.planar_uv(pos[1]),
		p_uv_map.planar_uv(pos[2]), p_uv_map.planar_uv(pos[3])
	};
	const Color shade(p_shade, p_shade, p_shade);
	const Color col[4] = { shade, shade, shade, shade };
	p_writer.quad(pos, uv, col, Vector3(0, 1, 0), Vector3(1, 0, 0), -1.0f);
}

void emit_hex_cell(PackedSurface::Writer &p_writer, const HexUvMap &p_uv_map, const HeightImageView &p_view,
				   const HexLayout &p_layout, const HexStyle &p_style, const RimProfile &p_rim,
				   int p_i, int p_j, double p_height_scale, double p_base_height, int p_filter, double p_height_power) {
	const Vector2 center = warped_cell_center(p_layout, p_style, p_i, p_j);
	const float top = cell_column_height(p_view, p_layout, p_style, p_rim, center, p_i, p_j,
										 p_height_scale, p_base_height, p_filter, p_height_power);
	Vector3 corners[6];
	place_cell_corners(p_layout, p_style, center, top, p_i, p_j, corners);
	const float tint = p_style.cell_tint(p_i, p_j);
	const float bottom = tint * (1.0f - p_style.ao * WALL_AO_SPAN * CLAMP(top / p_style.href, 0.0f, 1.0f));
	emit_hex_cap(p_writer, p_uv_map, corners, tint);
	emit_hex_walls(p_writer, p_uv_map, corners, tint, bottom);
}

} // namespace

Ref<ArrayMesh> HeightmapMesher::build_hex_mesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
											   double p_height_scale, double p_base_height, int p_filter,
											   double p_warp, double p_jitter, double p_gap, int64_t p_seed,
											   const Rect2 &p_flat_rect, double p_rim_boost, double p_rim_falloff,
											   double p_height_power, double p_ao, double p_variation, bool p_floor) const {
	const HeightImageView view = decode_height_image(p_image);
	if (!view.is_valid()) {
		return Ref<ArrayMesh>();
	}

	const HexLayout layout = make_hex_layout(p_size, p_verts);

	HexStyle style;
	style.warp = CLAMP((float)p_warp, 0.0f, 1.0f);
	style.jitter = CLAMP((float)p_jitter, 0.0f, 1.0f);
	style.gap = CLAMP((float)p_gap, 0.0f, MAX_HIVE_GAP);
	style.ao = CLAMP((float)p_ao, 0.0f, 1.0f);
	style.variation = CLAMP((float)p_variation, 0.0f, 1.0f);
	style.href = MAX(MIN_AO_HEIGHT_REFERENCE, (float)p_height_scale);
	style.seed = (uint32_t)((uint64_t)p_seed ^ ((uint64_t)p_seed >> 32));

	RimProfile rim;
	rim.enabled = p_rim_boost > 0.0 && p_flat_rect.has_area();
	rim.flat_rect = p_flat_rect;
	rim.boost = (float)p_rim_boost;
	rim.falloff = MAX(MIN_RIM_FALLOFF, (float)p_rim_falloff);

	const HexUvMap uv_map{ layout };
	const int64_t cell_count = (int64_t)layout.cols * (int64_t)layout.rows;
	const PackedSurface::Cursor floor_size = { p_floor ? FLOOR_VERTS : 0, p_floor ? FLOOR_INDICES : 0 };
	PackedSurface surface(floor_size.vertex + cell_count * CELL_VERTS,
						  floor_size.index + cell_count * CELL_INDICES, style.wants_color());
	if (p_floor) {
		PackedSurface::Writer writer = surface.writer_at({ 0, 0 });
		emit_hex_floor(writer, uv_map, layout, 1.0f - style.ao * FLOOR_AO_SPAN);
	}
	parallel_for_rows(layout.rows, [&](int p_begin, int p_end) {
		const int64_t first_cell = (int64_t)p_begin * (int64_t)layout.cols;
		PackedSurface::Writer writer = surface.writer_at({ floor_size.vertex + first_cell * CELL_VERTS,
														   floor_size.index + first_cell * CELL_INDICES });
		for (int j = p_begin; j < p_end; j++) {
			for (int i = 0; i < layout.cols; i++) {
				emit_hex_cell(writer, uv_map, view, layout, style, rim, i, j,
							  p_height_scale, p_base_height, p_filter, p_height_power);
			}
		}
	});
	return surface.commit();
}
