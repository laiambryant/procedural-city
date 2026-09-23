#ifndef HEIGHT_SAMPLING_H
#define HEIGHT_SAMPLING_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <vector>

namespace godot {

constexpr float HEIGHT_EPSILON = 0.0001f;

constexpr float HEIGHT_POWER_EPSILON = 1e-6f;

inline float apply_height_power(float p_value, float p_power) {
	if (p_power > 0.0f && Math::abs(p_power - 1.0f) > HEIGHT_POWER_EPSILON) {
		return Math::pow(p_value, p_power);
	}
	return p_value;
}

struct CellGrid {
	int cols = 1;
	int rows = 1;
	float cw = 1.0f;
	float cd = 1.0f;
	float ox = 0.0f;
	float oz = 0.0f;
};

CellGrid make_cell_grid(const Vector2 &p_size, const Vector2i &p_verts);

struct HeightImageView {
	Ref<Image> image;
	PackedByteArray data;
	const uint8_t *pixels = nullptr;
	int stride = 1;
	int width = 0;
	int height = 0;

	HeightImageView() = default;
	HeightImageView(HeightImageView &&) = default;
	HeightImageView &operator=(HeightImageView &&) = default;
	HeightImageView(const HeightImageView &) = delete;
	HeightImageView &operator=(const HeightImageView &) = delete;

	bool is_valid() const { return pixels != nullptr && stride > 0 && width > 0 && height > 0; }
};

HeightImageView decode_height_image(const Ref<Image> &p_source);

float sample_cell(const HeightImageView &p_view, int p_i, int p_j, int p_cols, int p_rows, int p_filter);
float sample_uv(const HeightImageView &p_view, float p_u, float p_v, int p_filter);

bool resolve_cell_heights(const Ref<Image> &p_image, const CellGrid &p_grid,
		double p_height_scale, double p_base_height, int p_filter, std::vector<float> &r_heights,
		double p_height_power = 1.0);

// Flattens every mesh-local XZ rect (origin at the mesh centre, like the cell
// grid) to zero in the field itself, so geometry, collision and cell heights
// all see the same open ground. Returns the number of rects applied.
int carve_height_image(const Ref<Image> &p_image, const Vector2 &p_mesh_size, const TypedArray<Rect2> &p_rects);

} // namespace godot

#endif // HEIGHT_SAMPLING_H
