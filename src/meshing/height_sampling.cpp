#include "meshing/height_sampling.h"

#include "meshing/heightmap_mesher.h"
#include "meshing/parallel_rows.h"

#include <godot_cpp/core/math.hpp>

using namespace godot;

static constexpr float MAX_BYTE_VALUE = 255.0f;

CellGrid godot::make_cell_grid(const Vector2 &p_size, const Vector2i &p_verts) {
	CellGrid g;
	g.cols = MAX(1, p_verts.x - 1);
	g.rows = MAX(1, p_verts.y - 1);
	g.cw = p_size.x / (float)g.cols;
	g.cd = p_size.y / (float)g.rows;
	g.ox = -p_size.x * 0.5f;
	g.oz = -p_size.y * 0.5f;
	return g;
}

static int red_stride_for_format(Image::Format p_format) {
	switch (p_format) {
		case Image::FORMAT_L8:
		case Image::FORMAT_R8:
			return 1;
		case Image::FORMAT_LA8:
		case Image::FORMAT_RG8:
			return 2;
		case Image::FORMAT_RGB8:
			return 3;
		case Image::FORMAT_RGBA8:
			return 4;
		default:
			return 0;
	}
}

HeightImageView godot::decode_height_image(const Ref<Image> &p_source) {
	HeightImageView view;
	if (p_source.is_null() || p_source->is_empty()) {
		return view;
	}
	Ref<Image> src = p_source;
	int stride = red_stride_for_format(src->get_format());
	if (stride == 0) {
		src = p_source->duplicate();
		src->convert(Image::FORMAT_RGBA8);
		view.image = src;
		stride = 4;
	}
	view.data = src->get_data();
	view.width = src->get_width();
	view.height = src->get_height();
	view.stride = stride;
	if (view.width <= 0 || view.height <= 0) {
		return view;
	}
	view.pixels = view.data.ptr();
	return view;
}

static float red_at(const HeightImageView &p_view, int p_x, int p_y) {
	return (float)p_view.pixels[((size_t)p_y * (size_t)p_view.width + (size_t)p_x) * (size_t)p_view.stride];
}

static inline uint64_t sum_bytes(const uint8_t *p_row, int p_count) {
	uint32_t a = 0, b = 0, c = 0, d = 0;
	int x = 0;
	for (; x + 4 <= p_count; x += 4) {
		a += p_row[x];
		b += p_row[x + 1];
		c += p_row[x + 2];
		d += p_row[x + 3];
	}
	for (; x < p_count; x++) {
		a += p_row[x];
	}
	return (uint64_t)a + (uint64_t)b + (uint64_t)c + (uint64_t)d;
}

static inline uint64_t sum_bytes_strided(const uint8_t *p_row, int p_count, int p_stride) {
	uint32_t a = 0, b = 0;
	const size_t step = (size_t)p_stride;
	int x = 0;
	for (; x + 2 <= p_count; x += 2) {
		a += p_row[(size_t)x * step];
		b += p_row[((size_t)x + 1) * step];
	}
	for (; x < p_count; x++) {
		a += p_row[(size_t)x * step];
	}
	return (uint64_t)a + (uint64_t)b;
}

static float sample_cell_nearest(const HeightImageView &p_view, int p_i, int p_j, int p_cols, int p_rows) {
	int px = (int)(((float)p_i + 0.5f) / (float)p_cols * (float)p_view.width);
	int py = (int)(((float)p_j + 0.5f) / (float)p_rows * (float)p_view.height);
	px = CLAMP(px, 0, p_view.width - 1);
	py = CLAMP(py, 0, p_view.height - 1);
	return red_at(p_view, px, py) / MAX_BYTE_VALUE;
}

static float sample_cell_box_average(const HeightImageView &p_view, int p_i, int p_j, int p_cols, int p_rows) {
	int x0 = (int)((float)p_i * (float)p_view.width / (float)p_cols);
	int x1 = (int)((float)(p_i + 1) * (float)p_view.width / (float)p_cols);
	int y0 = (int)((float)p_j * (float)p_view.height / (float)p_rows);
	int y1 = (int)((float)(p_j + 1) * (float)p_view.height / (float)p_rows);
	x0 = CLAMP(x0, 0, p_view.width - 1);
	y0 = CLAMP(y0, 0, p_view.height - 1);
	if (x1 <= x0) {
		x1 = x0 + 1;
	}
	if (y1 <= y0) {
		y1 = y0 + 1;
	}
	x1 = MIN(x1, p_view.width);
	y1 = MIN(y1, p_view.height);

	uint64_t sum = 0;
	for (int y = y0; y < y1; y++) {
		const uint8_t *row = p_view.pixels + (size_t)y * (size_t)p_view.width * (size_t)p_view.stride;
		if (p_view.stride == 1) {
			sum += sum_bytes(row + x0, x1 - x0);
		} else {
			sum += sum_bytes_strided(row + (size_t)x0 * (size_t)p_view.stride, x1 - x0, p_view.stride);
		}
	}
	const uint64_t count = (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0);
	return (float)sum / (float)count / MAX_BYTE_VALUE;
}

float godot::sample_cell(const HeightImageView &p_view, int p_i, int p_j, int p_cols, int p_rows, int p_filter) {
	if (p_filter == HeightmapMesher::FILTER_NEAREST) {
		return sample_cell_nearest(p_view, p_i, p_j, p_cols, p_rows);
	}
	return sample_cell_box_average(p_view, p_i, p_j, p_cols, p_rows);
}

static float sample_uv_bilinear(const HeightImageView &p_view, float p_u, float p_v) {
	const float x = p_u * (float)(p_view.width - 1);
	const float y = p_v * (float)(p_view.height - 1);
	const int x0 = (int)x;
	const int y0 = (int)y;
	const int x1 = MIN(x0 + 1, p_view.width - 1);
	const int y1 = MIN(y0 + 1, p_view.height - 1);
	const float fx = x - (float)x0;
	const float fy = y - (float)y0;
	const float a = red_at(p_view, x0, y0);
	const float b = red_at(p_view, x1, y0);
	const float c = red_at(p_view, x0, y1);
	const float d = red_at(p_view, x1, y1);
	return Math::lerp(Math::lerp(a, b, fx), Math::lerp(c, d, fx), fy) / MAX_BYTE_VALUE;
}

float godot::sample_uv(const HeightImageView &p_view, float p_u, float p_v, int p_filter) {
	p_u = CLAMP(p_u, 0.0f, 1.0f);
	p_v = CLAMP(p_v, 0.0f, 1.0f);
	if (p_filter == HeightmapMesher::FILTER_NEAREST) {
		const int px = CLAMP((int)(p_u * (float)p_view.width), 0, p_view.width - 1);
		const int py = CLAMP((int)(p_v * (float)p_view.height), 0, p_view.height - 1);
		return red_at(p_view, px, py) / MAX_BYTE_VALUE;
	}
	return sample_uv_bilinear(p_view, p_u, p_v);
}

static int min_rows_per_band_for_filter(bool p_nearest) {
	return p_nearest ? DEFAULT_MIN_ROWS_PER_BAND : HEAVY_MIN_ROWS_PER_BAND;
}

bool godot::resolve_cell_heights(const Ref<Image> &p_image, const CellGrid &p_grid,
		double p_height_scale, double p_base_height, int p_filter, std::vector<float> &r_heights,
		double p_height_power) {
	const HeightImageView view = decode_height_image(p_image);
	if (!view.is_valid()) {
		return false;
	}

	const float power = (float)p_height_power;
	const bool nearest = p_filter == HeightmapMesher::FILTER_NEAREST;

	r_heights.resize((size_t)p_grid.cols * (size_t)p_grid.rows);
	const int min_rows = min_rows_per_band_for_filter(nearest);
	parallel_for_rows(
			p_grid.rows,
			[&](int p_begin, int p_end) {
				for (int j = p_begin; j < p_end; j++) {
					float *out = r_heights.data() + (size_t)j * (size_t)p_grid.cols;
					for (int i = 0; i < p_grid.cols; i++) {
						const float raw = nearest
								? sample_cell_nearest(view, i, j, p_grid.cols, p_grid.rows)
								: sample_cell_box_average(view, i, j, p_grid.cols, p_grid.rows);
						const float box_h = (float)p_base_height + apply_height_power(raw, power) * (float)p_height_scale;
						out[i] = MAX(box_h, HEIGHT_EPSILON);
					}
				}
			},
			min_rows);
	return true;
}

int godot::carve_height_image(const Ref<Image> &p_image, const Vector2 &p_mesh_size, const TypedArray<Rect2> &p_rects) {
	if (p_image.is_null() || p_image->is_empty() || p_image->is_compressed() || p_rects.is_empty() ||
			p_mesh_size.x <= 0.0f || p_mesh_size.y <= 0.0f) {
		return 0;
	}
	const int width = p_image->get_width();
	const int height = p_image->get_height();
	const float sx = (float)width / p_mesh_size.x;
	const float sy = (float)height / p_mesh_size.y;
	const Vector2 origin = -p_mesh_size * 0.5f;
	int applied = 0;
	for (int64_t i = 0; i < p_rects.size(); i++) {
		const Rect2 rect = p_rects[i];
		if (!rect.is_finite()) {
			continue;
		}
		const Rect2 local = rect.abs();
		const int x0 = CLAMP((int)Math::floor((local.position.x - origin.x) * sx), 0, width);
		const int y0 = CLAMP((int)Math::floor((local.position.y - origin.y) * sy), 0, height);
		const int x1 = CLAMP((int)Math::ceil((local.get_end().x - origin.x) * sx), 0, width);
		const int y1 = CLAMP((int)Math::ceil((local.get_end().y - origin.y) * sy), 0, height);
		if (x1 <= x0 || y1 <= y0) {
			continue;
		}
		p_image->fill_rect(Rect2i(x0, y0, x1 - x0, y1 - y0), Color(0.0f, 0.0f, 0.0f, 1.0f));
		applied++;
	}
	return applied;
}
