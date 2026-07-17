#include "meshing/heightmap_mesher.h"

#include "meshing/baked_style.h"
#include "meshing/deterministic_noise.h"
#include "meshing/height_sampling.h"
#include "meshing/packed_surface.h"
#include "meshing/parallel_rows.h"

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

namespace {

// How the [0, 1] warp/jitter strengths translate into geometry. Cell-pitch
// values are fractions of one lattice cell.
constexpr float WARP_NOISE_PERIODS = 4.0f;		   // noise cycles across the lattice
constexpr float WARP_CENTER_RANGE_CELLS = 1.5f;	   // max centre drift at warp = 1
constexpr float JITTER_CENTER_RANGE_CELLS = 0.35f; // max centre offset at jitter = 1
constexpr float JITTER_HEIGHT_SPAN = 0.35f;		   // max +-height scale at jitter = 1
constexpr float JITTER_FOOTPRINT_SPAN = 0.18f;	   // max footprint shrink at jitter = 1
constexpr float JITTER_ROTATION_SPAN_RAD = 0.35f;  // max cap rotation at jitter = 1
// Rim towers keep between RIM_HEIGHT_FLOOR and 1.0 of the full rim boost, the
// remainder salted per cell so the wall reads as irregular towers.
constexpr float RIM_HEIGHT_FLOOR = 0.55f;

constexpr float MAX_HIVE_GAP = 0.5f;
// Lower bound for the rim falloff distance (metres), guarding the division
// that normalizes distance into the smoothstep.
constexpr float MIN_RIM_FALLOFF = 0.001f;

// Pointy-top hexagons on an offset lattice: odd rows shift half a cell so
// columns interlock at dx spacing, and the corner reach stretches the hexes
// onto the (dx, dz) cell pitch.
struct HexLayout {
	int cols = 1;
	int rows = 1;
	float sx = 1.0f;
	float sz = 1.0f;
	float dx = 1.0f;
	float dz = 1.0f;
	float ox = 0.0f;
	float oz = 0.0f;
	float reach_x = 1.0f;
	float reach_z = 1.0f;
	float warp_freq_x = 1.0f;
	float warp_freq_y = 1.0f;
};

HexLayout make_hex_layout(const Vector2 &p_size, const Vector2i &p_verts) {
	HexLayout l;
	l.cols = MAX(1, p_verts.x - 1);
	l.rows = MAX(1, p_verts.y - 1);
	l.sx = (p_size.x != 0.0f) ? p_size.x : 1.0f;
	l.sz = (p_size.y != 0.0f) ? p_size.y : 1.0f;
	l.dx = l.sx / (float)l.cols;
	l.dz = l.sz / (float)l.rows;
	l.ox = -l.sx * 0.5f;
	l.oz = -l.sz * 0.5f;
	l.reach_x = l.dx / Math::sqrt(3.0f);
	l.reach_z = l.dz / 1.5f;
	l.warp_freq_x = WARP_NOISE_PERIODS / (float)MAX(1, l.cols);
	l.warp_freq_y = WARP_NOISE_PERIODS / (float)MAX(1, l.rows);
	return l;
}

struct HexStyle {
	float warp = 0.0f;
	float jitter = 0.0f;
	float gap = 0.0f;
	float ao = 0.0f;
	float variation = 0.0f;
	float href = 1.0f;
	uint32_t seed = 0;

	bool wants_color() const { return ao > 0.0f || variation > 0.0f; }

	float cell_tint(int p_i, int p_j) const {
		return 1.0f - variation * VARIATION_TINT_SPAN * hash01(p_i, p_j, seed ^ SALT_CELL_TINT);
	}
};

struct RimProfile {
	bool enabled = false;
	Rect2 flat_rect;
	float boost = 0.0f;
	float falloff = 24.0f;
};

Vector2 warped_cell_center(const HexLayout &p_layout, const HexStyle &p_style, int p_i, int p_j) {
	const float row_shift = (p_j & 1) ? 0.5f : 0.0f;
	float cx = p_layout.ox + ((float)p_i + 0.5f + row_shift) * p_layout.dx;
	float cz = p_layout.oz + ((float)p_j + 0.5f) * p_layout.dz;

	const float wx = (value_noise((float)p_i * p_layout.warp_freq_x, (float)p_j * p_layout.warp_freq_y, p_style.seed ^ SALT_WARP_X) - 0.5f) * 2.0f;
	const float wz = (value_noise((float)p_i * p_layout.warp_freq_x, (float)p_j * p_layout.warp_freq_y, p_style.seed ^ SALT_WARP_Z) - 0.5f) * 2.0f;
	cx += wx * p_style.warp * p_layout.dx * WARP_CENTER_RANGE_CELLS;
	cz += wz * p_style.warp * p_layout.dz * WARP_CENTER_RANGE_CELLS;
	cx += (hash01(p_i, p_j, p_style.seed ^ SALT_JITTER_X) - 0.5f) * p_style.jitter * p_layout.dx * JITTER_CENTER_RANGE_CELLS;
	cz += (hash01(p_i, p_j, p_style.seed ^ SALT_JITTER_Z) - 0.5f) * p_style.jitter * p_layout.dz * JITTER_CENTER_RANGE_CELLS;
	return Vector2(cx, cz);
}

// distance_outside_rect is 0 inside the rect and grows with the XZ distance to
// its nearest edge outside it.
float distance_outside_rect(const Rect2 &p_rect, float p_x, float p_z) {
	const float over_x = MAX(0.0f, MAX(p_rect.position.x - p_x, p_x - (p_rect.position.x + p_rect.size.x)));
	const float over_z = MAX(0.0f, MAX(p_rect.position.y - p_z, p_z - (p_rect.position.y + p_rect.size.y)));
	return Math::sqrt(over_x * over_x + over_z * over_z);
}

// rim_lift raises cells outside the flat rect, smoothstepped over the falloff
// and salted per cell so the wall reads as irregular towers.
float rim_lift(const RimProfile &p_rim, const HexStyle &p_style, const Vector2 &p_center, int p_i, int p_j) {
	if (!p_rim.enabled) {
		return 0.0f;
	}
	const float dist = distance_outside_rect(p_rim.flat_rect, p_center.x, p_center.y);
	float t = CLAMP(dist / p_rim.falloff, 0.0f, 1.0f);
	t = t * t * (3.0f - 2.0f * t);
	return t * p_rim.boost * (RIM_HEIGHT_FLOOR + (1.0f - RIM_HEIGHT_FLOOR) * hash01(p_i, p_j, p_style.seed ^ SALT_RIM_HEIGHT));
}

float cell_column_height(const HeightImageView &p_view, const HexLayout &p_layout, const HexStyle &p_style,
						 const RimProfile &p_rim, const Vector2 &p_center, int p_i, int p_j,
						 double p_height_scale, double p_base_height, int p_filter, double p_height_power) {
	const float u = (p_center.x - p_layout.ox) / p_layout.sx;
	const float v = (p_center.y - p_layout.oz) / p_layout.sz;
	const float value = apply_height_power(sample_uv(p_view, u, v, p_filter), (float)p_height_power);
	float box_h = (float)p_base_height + value * (float)p_height_scale;
	box_h += rim_lift(p_rim, p_style, p_center, p_i, p_j);
	box_h *= 1.0f + (hash01(p_i, p_j, p_style.seed ^ SALT_HEIGHT_JITTER) - 0.5f) * p_style.jitter * JITTER_HEIGHT_SPAN;
	return MAX(box_h, HEIGHT_EPSILON);
}

// place_cell_corners shrinks the footprint by the gap and perturbs scale and
// rotation per cell so caps never read as a printed grid.
void place_cell_corners(const HexLayout &p_layout, const HexStyle &p_style,
						const Vector2 &p_center, float p_top, int p_i, int p_j, Vector3 r_corners[6]) {
	const float cell_scale = (1.0f - p_style.gap) * (1.0f - hash01(p_i, p_j, p_style.seed ^ SALT_CELL_SCALE) * p_style.jitter * JITTER_FOOTPRINT_SPAN);
	const float rot = (hash01(p_i, p_j, p_style.seed ^ SALT_CELL_ROTATION) - 0.5f) * p_style.jitter * JITTER_ROTATION_SPAN_RAD;
	for (int k = 0; k < 6; k++) {
		const float theta = (float)Math_PI / 6.0f + (float)k * (float)Math_PI / 3.0f + rot;
		r_corners[k] = Vector3(
				p_center.x + p_layout.reach_x * cell_scale * Math::cos(theta),
				p_top,
				p_center.y + p_layout.reach_z * cell_scale * Math::sin(theta));
	}
}

// One cell = a 6-corner cap fan (6 verts, 4 triangles) + 6 wall quads
// (4 verts, 2 triangles each); the floor is a single quad.
constexpr int64_t CELL_VERTS = 6 + 6 * 4;
constexpr int64_t CELL_INDICES = 4 * 3 + 6 * 2 * 3;
constexpr int64_t FLOOR_VERTS = 4;
constexpr int64_t FLOOR_INDICES = 6;

Vector3 normalized_or_zero(Vector3 p_v) {
	const float len = p_v.length();
	return (len > 0.0f) ? p_v / len : p_v;
}

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
