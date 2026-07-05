#include "heightmap_mesher.h"

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/csg_box3d.hpp>
#include <godot_cpp/classes/surface_tool.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

using namespace godot;

static const float HEIGHT_EPSILON = 0.0001f;

struct Grid {
	int cols = 1;
	int rows = 1;
	float cw = 1.0f;
	float cd = 1.0f;
	float ox = 0.0f;
	float oz = 0.0f;
};

static Grid make_grid(const Vector2 &p_size, const Vector2i &p_verts) {
	Grid g;
	g.cols = MAX(1, p_verts.x - 1);
	g.rows = MAX(1, p_verts.y - 1);
	g.cw = p_size.x / (float)g.cols;
	g.cd = p_size.y / (float)g.rows;
	g.ox = -p_size.x * 0.5f;
	g.oz = -p_size.y * 0.5f;
	return g;
}

static float sample_raw(const uint8_t *p_data, int p_w, int p_h, int p_i, int p_j, int p_cols, int p_rows, int p_filter) {
	if (p_filter == HeightmapMesher::FILTER_NEAREST) {
		int px = (int)(((float)p_i + 0.5f) / (float)p_cols * (float)p_w);
		int py = (int)(((float)p_j + 0.5f) / (float)p_rows * (float)p_h);
		px = CLAMP(px, 0, p_w - 1);
		py = CLAMP(py, 0, p_h - 1);
		return (float)p_data[(py * p_w + px) * 4] / 255.0f;
	}

	int x0 = (int)((float)p_i * (float)p_w / (float)p_cols);
	int x1 = (int)((float)(p_i + 1) * (float)p_w / (float)p_cols);
	int y0 = (int)((float)p_j * (float)p_h / (float)p_rows);
	int y1 = (int)((float)(p_j + 1) * (float)p_h / (float)p_rows);
	x0 = CLAMP(x0, 0, p_w - 1);
	y0 = CLAMP(y0, 0, p_h - 1);
	if (x1 <= x0) {
		x1 = x0 + 1;
	}
	if (y1 <= y0) {
		y1 = y0 + 1;
	}
	x1 = MIN(x1, p_w);
	y1 = MIN(y1, p_h);

	uint64_t sum = 0;
	uint64_t count = 0;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			sum += p_data[(y * p_w + x) * 4];
			count++;
		}
	}
	if (count == 0) {
		return 0.0f;
	}
	return (float)sum / (float)count / 255.0f;
}

static bool prepare(const Ref<Image> &p_image, Ref<Image> &r_img, std::vector<float> &r_heights,
		const Grid &p_grid, double p_height_scale, double p_base_height, int p_filter) {
	if (p_image.is_null() || p_image->is_empty()) {
		return false;
	}
	r_img.instantiate();
	r_img->copy_from(p_image);
	if (r_img->get_format() != Image::FORMAT_RGBA8) {
		r_img->convert(Image::FORMAT_RGBA8);
	}
	const PackedByteArray data = r_img->get_data();
	const uint8_t *d = data.ptr();
	const int w = r_img->get_width();
	const int h = r_img->get_height();
	if (w <= 0 || h <= 0 || d == nullptr) {
		return false;
	}

	r_heights.resize((size_t)p_grid.cols * (size_t)p_grid.rows);
	for (int j = 0; j < p_grid.rows; j++) {
		for (int i = 0; i < p_grid.cols; i++) {
			const float v = sample_raw(d, w, h, i, j, p_grid.cols, p_grid.rows, p_filter);
			float box_h = (float)p_base_height + v * (float)p_height_scale;
			if (box_h < HEIGHT_EPSILON) {
				box_h = HEIGHT_EPSILON;
			}
			r_heights[(size_t)j * (size_t)p_grid.cols + (size_t)i] = box_h;
		}
	}
	return true;
}

// --- Tiny deterministic value noise (no engine dependency, identical on every
// platform). Used by the hex backend for domain warp and per-cell jitter.

static inline uint32_t hash_u32(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

// hash01: lattice point + seed -> [0,1)
static inline float hash01(int p_x, int p_y, uint32_t p_seed) {
	uint32_t h = hash_u32((uint32_t)p_x * 0x8DA6B343u ^ (uint32_t)p_y * 0xD8163841u ^ p_seed);
	return (float)(h & 0x00FFFFFFu) / 16777216.0f;
}

// value_noise: smooth-interpolated lattice noise, [0,1)
static float value_noise(float p_x, float p_y, uint32_t p_seed) {
	const int x0 = (int)Math::floor(p_x);
	const int y0 = (int)Math::floor(p_y);
	const float fx = p_x - (float)x0;
	const float fy = p_y - (float)y0;
	const float ux = fx * fx * (3.0f - 2.0f * fx);
	const float uy = fy * fy * (3.0f - 2.0f * fy);
	const float a = hash01(x0, y0, p_seed);
	const float b = hash01(x0 + 1, y0, p_seed);
	const float c = hash01(x0, y0 + 1, p_seed);
	const float d = hash01(x0 + 1, y0 + 1, p_seed);
	return Math::lerp(Math::lerp(a, b, ux), Math::lerp(c, d, ux), uy);
}

// sample_uv: arbitrary-position sample of the height image (u/v in [0,1]).
// Nearest when requested, bilinear otherwise.
static float sample_uv(const uint8_t *p_data, int p_w, int p_h, float p_u, float p_v, int p_filter) {
	p_u = CLAMP(p_u, 0.0f, 1.0f);
	p_v = CLAMP(p_v, 0.0f, 1.0f);
	if (p_filter == HeightmapMesher::FILTER_NEAREST) {
		const int px = CLAMP((int)(p_u * (float)p_w), 0, p_w - 1);
		const int py = CLAMP((int)(p_v * (float)p_h), 0, p_h - 1);
		return (float)p_data[(py * p_w + px) * 4] / 255.0f;
	}
	const float x = p_u * (float)(p_w - 1);
	const float y = p_v * (float)(p_h - 1);
	const int x0 = (int)x;
	const int y0 = (int)y;
	const int x1 = MIN(x0 + 1, p_w - 1);
	const int y1 = MIN(y0 + 1, p_h - 1);
	const float fx = x - (float)x0;
	const float fy = y - (float)y0;
	const float a = (float)p_data[(y0 * p_w + x0) * 4];
	const float b = (float)p_data[(y0 * p_w + x1) * 4];
	const float c = (float)p_data[(y1 * p_w + x0) * 4];
	const float d = (float)p_data[(y1 * p_w + x1) * 4];
	return Math::lerp(Math::lerp(a, b, fx), Math::lerp(c, d, fx), fy) / 255.0f;
}

void HeightmapMesher::_bind_methods() {
	ClassDB::bind_method(D_METHOD("sample_height", "image", "i", "j", "cols", "rows", "filter"),
			&HeightmapMesher::sample_height);
	ClassDB::bind_method(D_METHOD("build_array_mesh", "image", "size", "verts", "height_scale", "base_height", "filter"),
			&HeightmapMesher::build_array_mesh);
	ClassDB::bind_method(D_METHOD("build_hex_mesh", "image", "size", "verts", "height_scale", "base_height", "filter", "warp", "jitter", "gap", "seed", "flat_rect", "rim_boost", "rim_falloff"),
			&HeightmapMesher::build_hex_mesh,
			DEFVAL(Rect2()), DEFVAL(0.0), DEFVAL(24.0));
	ClassDB::bind_method(D_METHOD("build_multimesh", "image", "size", "verts", "height_scale", "base_height", "filter"),
			&HeightmapMesher::build_multimesh);
	ClassDB::bind_method(D_METHOD("build_csg", "parent", "image", "size", "verts", "height_scale", "base_height", "filter"),
			&HeightmapMesher::build_csg);

	BIND_ENUM_CONSTANT(FILTER_NEAREST);
	BIND_ENUM_CONSTANT(FILTER_BOX_AVERAGE);
}

float HeightmapMesher::sample_height(const Ref<Image> &p_image, int p_i, int p_j, int p_cols, int p_rows, int p_filter) const {
	if (p_image.is_null() || p_image->is_empty()) {
		return 0.0f;
	}
	Ref<Image> img;
	img.instantiate();
	img->copy_from(p_image);
	if (img->get_format() != Image::FORMAT_RGBA8) {
		img->convert(Image::FORMAT_RGBA8);
	}
	const PackedByteArray data = img->get_data();
	const uint8_t *d = data.ptr();
	if (d == nullptr) {
		return 0.0f;
	}
	return sample_raw(d, img->get_width(), img->get_height(), p_i, p_j, MAX(1, p_cols), MAX(1, p_rows), p_filter);
}

Ref<ArrayMesh> HeightmapMesher::build_array_mesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
		double p_height_scale, double p_base_height, int p_filter) const {
	const Grid g = make_grid(p_size, p_verts);
	Ref<Image> img;
	std::vector<float> heights;
	if (!prepare(p_image, img, heights, g, p_height_scale, p_base_height, p_filter)) {
		return Ref<ArrayMesh>();
	}

	const float sx = (p_size.x != 0.0f) ? p_size.x : 1.0f;
	const float sz = (p_size.y != 0.0f) ? p_size.y : 1.0f;

	Ref<SurfaceTool> st;
	st.instantiate();
	st->begin(Mesh::PRIMITIVE_TRIANGLES);

	auto add_vertex = [&](const Vector3 &p, const Vector3 &n) {
		st->set_normal(n);
		st->set_uv(Vector2((p.x - g.ox) / sx, (p.z - g.oz) / sz));
		st->add_vertex(p);
	};
	auto add_tri = [&](const Vector3 &a, const Vector3 &b, const Vector3 &c, const Vector3 &n) {
		add_vertex(a, n);
		add_vertex(b, n);
		add_vertex(c, n);
	};

	const Vector3 n_up(0, 1, 0);
	const Vector3 n_south(0, 0, -1);
	const Vector3 n_north(0, 0, 1);
	const Vector3 n_west(-1, 0, 0);
	const Vector3 n_east(1, 0, 0);

	for (int j = 0; j < g.rows; j++) {
		for (int i = 0; i < g.cols; i++) {
			const float y1 = heights[(size_t)j * (size_t)g.cols + (size_t)i];
			const float x0 = g.ox + (float)i * g.cw;
			const float x1 = x0 + g.cw;
			const float z0 = g.oz + (float)j * g.cd;
			const float z1 = z0 + g.cd;

			const Vector3 a1(x0, y1, z0);
			const Vector3 b1(x1, y1, z0);
			const Vector3 c1(x1, y1, z1);
			const Vector3 d1(x0, y1, z1);
			add_tri(a1, b1, c1, n_up);
			add_tri(a1, c1, d1, n_up);

			const float nh_south = (j > 0) ? heights[(size_t)(j - 1) * (size_t)g.cols + (size_t)i] : 0.0f;
			const float nh_north = (j < g.rows - 1) ? heights[(size_t)(j + 1) * (size_t)g.cols + (size_t)i] : 0.0f;
			const float nh_west = (i > 0) ? heights[(size_t)j * (size_t)g.cols + (size_t)(i - 1)] : 0.0f;
			const float nh_east = (i < g.cols - 1) ? heights[(size_t)j * (size_t)g.cols + (size_t)(i + 1)] : 0.0f;

			if (y1 > nh_south) {
				const float lo = nh_south;
				add_tri(Vector3(x0, lo, z0), Vector3(x1, lo, z0), Vector3(x1, y1, z0), n_south);
				add_tri(Vector3(x0, lo, z0), Vector3(x1, y1, z0), Vector3(x0, y1, z0), n_south);
			}
			if (y1 > nh_north) {
				const float lo = nh_north;
				add_tri(Vector3(x1, lo, z1), Vector3(x0, lo, z1), Vector3(x0, y1, z1), n_north);
				add_tri(Vector3(x1, lo, z1), Vector3(x0, y1, z1), Vector3(x1, y1, z1), n_north);
			}
			if (y1 > nh_west) {
				const float lo = nh_west;
				add_tri(Vector3(x0, lo, z0), Vector3(x0, y1, z0), Vector3(x0, y1, z1), n_west);
				add_tri(Vector3(x0, lo, z0), Vector3(x0, y1, z1), Vector3(x0, lo, z1), n_west);
			}
			if (y1 > nh_east) {
				const float lo = nh_east;
				add_tri(Vector3(x1, lo, z0), Vector3(x1, lo, z1), Vector3(x1, y1, z1), n_east);
				add_tri(Vector3(x1, lo, z0), Vector3(x1, y1, z1), Vector3(x1, y1, z0), n_east);
			}
		}
	}

	st->generate_tangents();
	return st->commit();
}

Ref<ArrayMesh> HeightmapMesher::build_hex_mesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
		double p_height_scale, double p_base_height, int p_filter,
		double p_warp, double p_jitter, double p_gap, int64_t p_seed,
		const Rect2 &p_flat_rect, double p_rim_boost, double p_rim_falloff) const {
	if (p_image.is_null() || p_image->is_empty()) {
		return Ref<ArrayMesh>();
	}
	Ref<Image> img;
	img.instantiate();
	img->copy_from(p_image);
	if (img->get_format() != Image::FORMAT_RGBA8) {
		img->convert(Image::FORMAT_RGBA8);
	}
	const PackedByteArray data = img->get_data();
	const uint8_t *d = data.ptr();
	const int w = img->get_width();
	const int h = img->get_height();
	if (w <= 0 || h <= 0 || d == nullptr) {
		return Ref<ArrayMesh>();
	}

	const int cols = MAX(1, p_verts.x - 1);
	const int rows = MAX(1, p_verts.y - 1);
	const float sx = (p_size.x != 0.0f) ? p_size.x : 1.0f;
	const float sz = (p_size.y != 0.0f) ? p_size.y : 1.0f;
	const float dx = sx / (float)cols;
	const float dz = sz / (float)rows;
	const float ox = -sx * 0.5f;
	const float oz = -sz * 0.5f;
	// Pointy-top hex reach: horizontal so odd rows interlock at dx spacing,
	// vertical so rows tile at 1.5R spacing stretched onto dz.
	const float hx = dx / Math::sqrt(3.0f);
	const float hz = dz / 1.5f;

	const float warp = CLAMP((float)p_warp, 0.0f, 1.0f);
	const float jitter = CLAMP((float)p_jitter, 0.0f, 1.0f);
	const float gap = CLAMP((float)p_gap, 0.0f, 0.5f);
	const uint32_t seed = (uint32_t)((uint64_t)p_seed ^ ((uint64_t)p_seed >> 32));
	const bool rim_enabled = p_rim_boost > 0.0 && p_flat_rect.has_area();
	const float rim_boost = (float)p_rim_boost;
	const float rim_falloff = MAX(0.001f, (float)p_rim_falloff);
	// Low-frequency warp field: ~4 undulations across the map.
	const float warp_freq_x = 4.0f / (float)MAX(1, cols);
	const float warp_freq_y = 4.0f / (float)MAX(1, rows);

	// Non-indexed emission into preallocated arrays (flat shading needs split
	// vertices anyway); tangents are computed per face instead of running
	// mikktspace over ~48 verts/cell. 12 cap + 36 wall verts per cell.
	const int64_t vert_count = (int64_t)cols * (int64_t)rows * 48;
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedFloat32Array tangents;
	vertices.resize(vert_count);
	normals.resize(vert_count);
	uvs.resize(vert_count);
	tangents.resize(vert_count * 4);
	Vector3 *vp = vertices.ptrw();
	Vector3 *np = normals.ptrw();
	Vector2 *uvp = uvs.ptrw();
	float *tp = tangents.ptrw();
	int64_t vi = 0;

	auto add_vertex = [&](const Vector3 &p, const Vector3 &n, const Vector3 &t, float w) {
		vp[vi] = p;
		np[vi] = n;
		uvp[vi] = Vector2((p.x - ox) / sx, (p.z - oz) / sz);
		tp[vi * 4 + 0] = t.x;
		tp[vi * 4 + 1] = t.y;
		tp[vi * 4 + 2] = t.z;
		tp[vi * 4 + 3] = w;
		vi++;
	};

	const Vector3 n_up(0, 1, 0);
	// Planar XZ UVs with N = +Y: u grows with +x, v with +z -> T = +X, w = -1.
	const Vector3 t_up(1, 0, 0);
	Vector3 corners[6];

	for (int j = 0; j < rows; j++) {
		for (int i = 0; i < cols; i++) {
			const float row_shift = (j & 1) ? 0.5f : 0.0f;
			float cx = ox + ((float)i + 0.5f + row_shift) * dx;
			float cz = oz + ((float)j + 0.5f) * dz;

			// Domain warp: bend the lattice with smooth noise so columns drift
			// off-grid; jitter adds per-cell scatter on top.
			const float wx = (value_noise((float)i * warp_freq_x, (float)j * warp_freq_y, seed ^ 0x51ED270Bu) - 0.5f) * 2.0f;
			const float wz = (value_noise((float)i * warp_freq_x, (float)j * warp_freq_y, seed ^ 0x9E3779B9u) - 0.5f) * 2.0f;
			cx += wx * warp * dx * 1.5f;
			cz += wz * warp * dz * 1.5f;
			cx += (hash01(i, j, seed ^ 0x2545F491u) - 0.5f) * jitter * dx * 0.35f;
			cz += (hash01(i, j, seed ^ 0x6C8E9CF5u) - 0.5f) * jitter * dz * 0.35f;

			// Height from the (warped) displacement sample, with a subtle
			// per-cell growth variation so caps never sit perfectly level.
			const float u = (cx - ox) / sx;
			const float v = (cz - oz) / sz;
			float value = sample_uv(d, w, h, u, v, p_filter);
			float box_h = (float)p_base_height + value * (float)p_height_scale;

			// Rim: distance of the (warped) centre outside the flat rect,
			// smoothstepped over the falloff, with a fresh per-cell salt so
			// the canyon wall reads as irregular towers, never a flat rampart.
			if (rim_enabled) {
				const float over_x = MAX(0.0f, MAX(p_flat_rect.position.x - cx, cx - (p_flat_rect.position.x + p_flat_rect.size.x)));
				const float over_z = MAX(0.0f, MAX(p_flat_rect.position.y - cz, cz - (p_flat_rect.position.y + p_flat_rect.size.y)));
				const float dist = Math::sqrt(over_x * over_x + over_z * over_z);
				float t = CLAMP(dist / rim_falloff, 0.0f, 1.0f);
				t = t * t * (3.0f - 2.0f * t);
				box_h += t * rim_boost * (0.55f + 0.45f * hash01(i, j, seed ^ 0x7F4A7C15u));
			}

			box_h *= 1.0f + (hash01(i, j, seed ^ 0xB5297A4Du) - 0.5f) * jitter * 0.35f;
			if (box_h < HEIGHT_EPSILON) {
				box_h = HEIGHT_EPSILON;
			}

			// Cell footprint: shrunk by the gap, slightly irregular in scale
			// and rotation per cell.
			const float cell_scale = (1.0f - gap) * (1.0f - hash01(i, j, seed ^ 0x68E31DA4u) * jitter * 0.18f);
			const float rot = (hash01(i, j, seed ^ 0x1B56C4E9u) - 0.5f) * jitter * 0.35f;
			for (int k = 0; k < 6; k++) {
				const float theta = (float)Math_PI / 6.0f + (float)k * (float)Math_PI / 3.0f + rot;
				corners[k] = Vector3(
						cx + hx * cell_scale * Math::cos(theta),
						box_h,
						cz + hz * cell_scale * Math::sin(theta));
			}

			// Top cap: fan around corner 0.
			for (int k = 1; k < 5; k++) {
				add_vertex(corners[0], n_up, t_up, -1.0f);
				add_vertex(corners[k], n_up, t_up, -1.0f);
				add_vertex(corners[k + 1], n_up, t_up, -1.0f);
			}

			// Walls down to the floor; the gap keeps them visible, so no
			// neighbour-culling is attempted (cells are freestanding comb).
			// Wall UVs are degenerate in V (planar mapping), so the tangent
			// falls back to the horizontal edge direction.
			for (int k = 0; k < 6; k++) {
				const Vector3 &ca = corners[k];
				const Vector3 &cb = corners[(k + 1) % 6];
				const Vector3 edge = cb - ca;
				Vector3 n_out(edge.z, 0.0f, -edge.x);
				const float len = n_out.length();
				if (len > 0.0f) {
					n_out /= len;
				}
				Vector3 t_wall(edge.x, 0.0f, edge.z);
				const float tlen = t_wall.length();
				if (tlen > 0.0f) {
					t_wall /= tlen;
				}
				const Vector3 a_lo(ca.x, 0.0f, ca.z);
				const Vector3 b_lo(cb.x, 0.0f, cb.z);
				add_vertex(a_lo, n_out, t_wall, 1.0f);
				add_vertex(b_lo, n_out, t_wall, 1.0f);
				add_vertex(cb, n_out, t_wall, 1.0f);
				add_vertex(a_lo, n_out, t_wall, 1.0f);
				add_vertex(cb, n_out, t_wall, 1.0f);
				add_vertex(ca, n_out, t_wall, 1.0f);
			}
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TANGENT] = tangents;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

Ref<MultiMesh> HeightmapMesher::build_multimesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
		double p_height_scale, double p_base_height, int p_filter) const {
	const Grid g = make_grid(p_size, p_verts);
	Ref<Image> img;
	std::vector<float> heights;
	if (!prepare(p_image, img, heights, g, p_height_scale, p_base_height, p_filter)) {
		return Ref<MultiMesh>();
	}

	Ref<BoxMesh> box;
	box.instantiate();
	box->set_size(Vector3(1, 1, 1));

	Ref<MultiMesh> mm;
	mm.instantiate();
	mm->set_transform_format(MultiMesh::TRANSFORM_3D);
	mm->set_mesh(box);
	mm->set_instance_count(g.cols * g.rows);

	int idx = 0;
	for (int j = 0; j < g.rows; j++) {
		for (int i = 0; i < g.cols; i++) {
			const float box_h = heights[(size_t)j * (size_t)g.cols + (size_t)i];
			const float cx = g.ox + ((float)i + 0.5f) * g.cw;
			const float cz = g.oz + ((float)j + 0.5f) * g.cd;
			const Basis basis = Basis().scaled(Vector3(g.cw, box_h, g.cd));
			mm->set_instance_transform(idx++, Transform3D(basis, Vector3(cx, box_h * 0.5f, cz)));
		}
	}
	return mm;
}

void HeightmapMesher::build_csg(Node3D *p_parent, const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
		double p_height_scale, double p_base_height, int p_filter) const {
	if (p_parent == nullptr) {
		return;
	}
	const Grid g = make_grid(p_size, p_verts);
	Ref<Image> img;
	std::vector<float> heights;
	if (!prepare(p_image, img, heights, g, p_height_scale, p_base_height, p_filter)) {
		return;
	}

	for (int j = 0; j < g.rows; j++) {
		for (int i = 0; i < g.cols; i++) {
			const float box_h = heights[(size_t)j * (size_t)g.cols + (size_t)i];
			const float cx = g.ox + ((float)i + 0.5f) * g.cw;
			const float cz = g.oz + ((float)j + 0.5f) * g.cd;
			CSGBox3D *box = memnew(CSGBox3D);
			box->set_size(Vector3(g.cw, box_h, g.cd));
			p_parent->add_child(box);
			box->set_position(Vector3(cx, box_h * 0.5f, cz));
		}
	}
}
