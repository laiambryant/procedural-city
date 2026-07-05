#ifndef HEIGHTMAP_MESHER_H
#define HEIGHTMAP_MESHER_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace godot {

// HeightmapMesher converts a grayscale displacement Image into block geometry,
// mirroring the Blender "Grid -> Extrude Mesh per face" graph: one extruded box
// per grid cell, its height driven by the sampled pixel value.
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
			double p_height_scale, double p_base_height, int p_filter) const;
	// Hex-prism honeycomb backend: pointy-top hexagonal columns on an offset
	// lattice, with domain-warped sampling and per-cell jitter so the result
	// reads as grown comb rather than printed grid. warp/jitter in [0,1],
	// gap in [0,0.5] (visible cell separation), seed drives all the noise.
	// Rim: cells outside flat_rect (mesh-local XZ) gain up to rim_boost extra
	// height, smoothstepped over rim_falloff metres — towering canyon walls
	// around a kept-low interior. rim_boost <= 0 or an empty rect disables it
	// (the default path stays bit-identical to the pre-rim output).
	Ref<ArrayMesh> build_hex_mesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
			double p_height_scale, double p_base_height, int p_filter,
			double p_warp, double p_jitter, double p_gap, int64_t p_seed,
			const Rect2 &p_flat_rect = Rect2(), double p_rim_boost = 0.0, double p_rim_falloff = 24.0) const;
	Ref<MultiMesh> build_multimesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
			double p_height_scale, double p_base_height, int p_filter) const;
	void build_csg(Node3D *p_parent, const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
			double p_height_scale, double p_base_height, int p_filter) const;

	HeightmapMesher() {}
	~HeightmapMesher() {}
};

} // namespace godot

VARIANT_ENUM_CAST(godot::HeightmapMesher::SampleFilter);

#endif // HEIGHTMAP_MESHER_H
