#ifndef HEIGHTMAP_MESHER_H
#define HEIGHTMAP_MESHER_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <vector>

namespace godot {

inline constexpr float MAX_BLOCK_INSET = 0.45f;

inline constexpr int MAX_GEOMETRY_CHUNKS = 16;

class HeightmapMesher : public RefCounted {
	GDCLASS(HeightmapMesher, RefCounted)

public:
	enum SampleFilter {
		FILTER_NEAREST = 0,
		FILTER_BOX_AVERAGE = 1,
	};

protected:
	static void _bind_methods();

public:
	float sample_height(const Ref<Image> &p_image, int p_i, int p_j, int p_cols, int p_rows, int p_filter) const;

	Ref<ArrayMesh> build_array_mesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
			double p_height_scale, double p_base_height, int p_filter,
			double p_height_power = 1.0, double p_inset = 0.0,
			int64_t p_seed = 0, double p_ao = 0.0, double p_variation = 0.0,
			double p_clip_below_height = 0.0) const;
	Ref<ArrayMesh> build_array_mesh_with_heights(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
			double p_height_scale, double p_base_height, int p_filter,
			double p_height_power, double p_inset, int64_t p_seed,
			double p_ao, double p_variation, double p_clip_below_height,
			std::vector<float> &r_heights) const;
	std::vector<Ref<ArrayMesh>> build_array_mesh_chunks(const Ref<Image> &p_image, const Vector2 &p_size,
			const Vector2i &p_verts, double p_height_scale, double p_base_height,
			int p_filter, double p_height_power, double p_inset, int64_t p_seed,
			double p_ao, double p_variation, double p_clip_below_height,
			int p_chunks, std::vector<float> &r_heights) const;
	TypedArray<ArrayMesh> build_array_mesh_chunks_array(const Ref<Image> &p_image, const Vector2 &p_size,
			const Vector2i &p_verts, double p_height_scale, double p_base_height,
			int p_filter, double p_height_power, double p_inset, int64_t p_seed,
			double p_ao, double p_variation, double p_clip_below_height,
			int p_chunks) const;
	Ref<ArrayMesh> build_hex_mesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
			double p_height_scale, double p_base_height, int p_filter,
			double p_warp, double p_jitter, double p_gap, int64_t p_seed,
			const Rect2 &p_flat_rect = Rect2(), double p_rim_boost = 0.0, double p_rim_falloff = 24.0,
			double p_height_power = 1.0,
			double p_ao = 0.0, double p_variation = 0.0, bool p_floor = false) const;
	Ref<MultiMesh> build_multimesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
			double p_height_scale, double p_base_height, int p_filter,
			double p_height_power = 1.0, double p_inset = 0.0) const;
	void build_csg(Node3D *p_parent, const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
			double p_height_scale, double p_base_height, int p_filter,
			double p_height_power = 1.0, double p_inset = 0.0) const;

	HeightmapMesher() {}
	~HeightmapMesher() {}
};

} // namespace godot

VARIANT_ENUM_CAST(godot::HeightmapMesher::SampleFilter);

#endif // HEIGHTMAP_MESHER_H
