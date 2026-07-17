#include "meshing/heightmap_mesher.h"

#include "meshing/height_sampling.h"
#include "meshing/parallel_rows.h"

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/csg_box3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

using namespace godot;

void HeightmapMesher::_bind_methods() {
	ClassDB::bind_method(D_METHOD("sample_height", "image", "i", "j", "cols", "rows", "filter"),
						 &HeightmapMesher::sample_height);
	ClassDB::bind_method(D_METHOD("build_array_mesh", "image", "size", "verts", "height_scale", "base_height", "filter", "height_power", "inset", "seed", "ao", "variation"),
						 &HeightmapMesher::build_array_mesh,
						 DEFVAL(1.0), DEFVAL(0.0), DEFVAL(0), DEFVAL(0.0), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("build_hex_mesh", "image", "size", "verts", "height_scale", "base_height", "filter", "warp", "jitter", "gap", "seed", "flat_rect", "rim_boost", "rim_falloff", "height_power", "ao", "variation", "floor"),
						 &HeightmapMesher::build_hex_mesh,
						 DEFVAL(Rect2()), DEFVAL(0.0), DEFVAL(24.0), DEFVAL(1.0), DEFVAL(0.0), DEFVAL(0.0), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("build_multimesh", "image", "size", "verts", "height_scale", "base_height", "filter", "height_power", "inset"),
						 &HeightmapMesher::build_multimesh,
						 DEFVAL(1.0), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("build_csg", "parent", "image", "size", "verts", "height_scale", "base_height", "filter", "height_power", "inset"),
						 &HeightmapMesher::build_csg,
						 DEFVAL(1.0), DEFVAL(0.0));

	BIND_ENUM_CONSTANT(FILTER_NEAREST);
	BIND_ENUM_CONSTANT(FILTER_BOX_AVERAGE);
}

float HeightmapMesher::sample_height(const Ref<Image> &p_image, int p_i, int p_j, int p_cols, int p_rows, int p_filter) const {
	const HeightImageView view = decode_height_image(p_image);
	if (!view.is_valid()) {
		return 0.0f;
	}
	return sample_cell(view, p_i, p_j, MAX(1, p_cols), MAX(1, p_rows), p_filter);
}

// The multimesh buffer stores each instance as a row-major 3x4 transform; a
// scale-only basis leaves just the diagonal and the origin column.
static constexpr int64_t INSTANCE_TRANSFORM_FLOATS = 12;

static void write_instance_transform(float *p_out, float p_sx, float p_sy, float p_sz,
									 float p_x, float p_y, float p_z) {
	p_out[0] = p_sx;
	p_out[1] = 0.0f;
	p_out[2] = 0.0f;
	p_out[3] = p_x;
	p_out[4] = 0.0f;
	p_out[5] = p_sy;
	p_out[6] = 0.0f;
	p_out[7] = p_y;
	p_out[8] = 0.0f;
	p_out[9] = 0.0f;
	p_out[10] = p_sz;
	p_out[11] = p_z;
}

Ref<MultiMesh> HeightmapMesher::build_multimesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
												double p_height_scale, double p_base_height, int p_filter, double p_height_power, double p_inset) const {
	const CellGrid grid = make_cell_grid(p_size, p_verts);
	std::vector<float> heights;
	if (!resolve_cell_heights(p_image, grid, p_height_scale, p_base_height, p_filter, heights, p_height_power)) {
		return Ref<MultiMesh>();
	}

	const float inset = CLAMP((float)p_inset, 0.0f, MAX_BLOCK_INSET);
	const float fw = grid.cw * (1.0f - 2.0f * inset);
	const float fd = grid.cd * (1.0f - 2.0f * inset);

	Ref<BoxMesh> box;
	box.instantiate();
	box->set_size(Vector3(1, 1, 1));

	Ref<MultiMesh> mm;
	mm.instantiate();
	mm->set_transform_format(MultiMesh::TRANSFORM_3D);
	mm->set_mesh(box);
	mm->set_instance_count(grid.cols * grid.rows);

	PackedFloat32Array buffer;
	buffer.resize((int64_t)grid.cols * (int64_t)grid.rows * INSTANCE_TRANSFORM_FLOATS);
	float *bp = buffer.ptrw();
	parallel_for_rows(grid.rows, [&](int p_begin, int p_end) {
		for (int j = p_begin; j < p_end; j++) {
			for (int i = 0; i < grid.cols; i++) {
				const size_t cell = (size_t)j * (size_t)grid.cols + (size_t)i;
				const float box_h = heights[cell];
				const float cx = grid.ox + ((float)i + 0.5f) * grid.cw;
				const float cz = grid.oz + ((float)j + 0.5f) * grid.cd;
				write_instance_transform(bp + cell * INSTANCE_TRANSFORM_FLOATS, fw, box_h, fd, cx, box_h * 0.5f, cz);
			}
		}
	});
	mm->set_buffer(buffer);
	return mm;
}

void HeightmapMesher::build_csg(Node3D *p_parent, const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
								double p_height_scale, double p_base_height, int p_filter, double p_height_power, double p_inset) const {
	if (p_parent == nullptr) {
		return;
	}
	const CellGrid grid = make_cell_grid(p_size, p_verts);
	std::vector<float> heights;
	if (!resolve_cell_heights(p_image, grid, p_height_scale, p_base_height, p_filter, heights, p_height_power)) {
		return;
	}

	const float inset = CLAMP((float)p_inset, 0.0f, MAX_BLOCK_INSET);
	const float fw = grid.cw * (1.0f - 2.0f * inset);
	const float fd = grid.cd * (1.0f - 2.0f * inset);

	for (int j = 0; j < grid.rows; j++) {
		for (int i = 0; i < grid.cols; i++) {
			const float box_h = heights[(size_t)j * (size_t)grid.cols + (size_t)i];
			const float cx = grid.ox + ((float)i + 0.5f) * grid.cw;
			const float cz = grid.oz + ((float)j + 0.5f) * grid.cd;
			CSGBox3D *box = memnew(CSGBox3D);
			box->set_size(Vector3(fw, box_h, fd));
			p_parent->add_child(box);
			box->set_position(Vector3(cx, box_h * 0.5f, cz));
		}
	}
}
