#ifndef BLOCK_STYLE_H
#define BLOCK_STYLE_H

#include "meshing/baked_style.h"
#include "meshing/block_layout.h"
#include "meshing/deterministic_noise.h"
#include "meshing/height_sampling.h"

#include <godot_cpp/variant/color.hpp>

#include <vector>

namespace godot {

// The baked-look half of the block backend: per-cell tint and the ambient
// occlusion the roofs and walls carry as vertex colours. Header-only and
// inline because every cell of every city goes through BlockShader, where an
// out-of-line call would cost more than the maths it wraps.

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

} // namespace godot

#endif // BLOCK_STYLE_H
