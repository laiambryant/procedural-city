#ifndef HEIGHT_SAMPLING_H
#define HEIGHT_SAMPLING_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <vector>

namespace godot {

constexpr float HEIGHT_EPSILON = 0.0001f;

// Powers within this distance of 1.0 are treated as linear (no remap).
constexpr float HEIGHT_POWER_EPSILON = 1e-6f;

// apply_height_power remaps a normalized height sample (v^power; 1 = linear).
// Non-positive powers are ignored rather than inverting the map.
inline float apply_height_power(float p_value, float p_power) {
	if (p_power > 0.0f && Math::abs(p_power - 1.0f) > HEIGHT_POWER_EPSILON) {
		return Math::pow(p_value, p_power);
	}
	return p_value;
}

// CellGrid is the block lattice a mesh backend fills: cols x rows cells of
// cw x cd metres, with (ox, oz) the north-west corner in mesh-local space.
struct CellGrid {
	int cols = 1;
	int rows = 1;
	float cw = 1.0f;
	float cd = 1.0f;
	float ox = 0.0f;
	float oz = 0.0f;
};

CellGrid make_cell_grid(const Vector2 &p_size, const Vector2i &p_verts);

// HeightImageView aliases the source image bytes and records the pixel stride;
// no full-size red-channel extraction is needed for RGB/RGBA inputs. Move-only
// so `pixels` can never outlive the buffer it aliases.
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

// resolve_cell_heights samples one world-space box height per grid cell,
// applying the height_power remap (v^power; 1 = linear), base height, height
// scale and the minimum-height clamp.
bool resolve_cell_heights(const Ref<Image> &p_image, const CellGrid &p_grid,
						  double p_height_scale, double p_base_height, int p_filter, std::vector<float> &r_heights,
						  double p_height_power = 1.0);

} // namespace godot

#endif // HEIGHT_SAMPLING_H
