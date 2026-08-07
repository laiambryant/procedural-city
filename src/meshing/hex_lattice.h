#ifndef HEX_LATTICE_H
#define HEX_LATTICE_H

#include "meshing/baked_style.h"
#include "meshing/deterministic_noise.h"
#include "meshing/height_sampling.h"

#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace godot {

// The lattice half of the hex backend: where a cell sits once the domain warp
// and per-cell jitter have moved it, how tall its column is, and where its six
// corners land. Header-only and inline because every cell of every hive runs
// through all of it.

// How the [0, 1] warp/jitter strengths translate into geometry. Cell-pitch
// values are fractions of one lattice cell.
constexpr float WARP_NOISE_PERIODS = 4.0f; // noise cycles across the lattice
constexpr float WARP_CENTER_RANGE_CELLS = 1.5f; // max centre drift at warp = 1
constexpr float JITTER_CENTER_RANGE_CELLS = 0.35f; // max centre offset at jitter = 1
constexpr float JITTER_HEIGHT_SPAN = 0.35f; // max +-height scale at jitter = 1
constexpr float JITTER_FOOTPRINT_SPAN = 0.18f; // max footprint shrink at jitter = 1
constexpr float JITTER_ROTATION_SPAN_RAD = 0.35f; // max cap rotation at jitter = 1
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

} // namespace godot

#endif // HEX_LATTICE_H
