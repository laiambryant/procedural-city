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

// Largest allowed block inset (fraction of a cell per side). Past this the
// remaining footprint is too small to read as a building; the inspector range
// for block_inset mirrors it.
inline constexpr float MAX_BLOCK_INSET = 0.45f;

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

	// Walls always use "draped" UVs — the roof's planar mapping continued down
	// the face — instead of the old planar projection that smeared one texture
	// row across every vertical face. Tops stay single flat quads. Style
	// extras are default-off: ao bakes vertex-colour ambient occlusion (canyon
	// walls darken toward the floor, roofs darken beside taller neighbours)
	// and variation applies a seeded per-cell luminance tint that breaks
	// texture repetition. height_power remaps the sampled value (v^power)
	// before scaling: > 1 thins the skyline into a few tall towers, < 1 raises
	// the low blocks. inset > 0 shrinks every block footprint by that fraction
	// of the cell per side, switching from merged blocks (shared walls culled)
	// to freestanding buildings with streets and an automatic ground plane.
	Ref<ArrayMesh> build_array_mesh(const Ref<Image> &p_image, const Vector2 &p_size, const Vector2i &p_verts,
									double p_height_scale, double p_base_height, int p_filter,
									double p_height_power = 1.0, double p_inset = 0.0,
									int64_t p_seed = 0, double p_ao = 0.0, double p_variation = 0.0) const;
	// Hex-prism honeycomb backend: pointy-top hexagonal columns on an offset
	// lattice, with domain-warped sampling and per-cell jitter so the result
	// reads as grown comb rather than printed grid. warp/jitter in [0,1],
	// gap in [0,0.5] (visible cell separation), seed drives all the noise.
	// Rim: cells outside flat_rect (mesh-local XZ) gain up to rim_boost extra
	// height, smoothstepped over rim_falloff metres — towering canyon walls
	// around a kept-low interior. rim_boost <= 0 or an empty rect disables it.
	// ao/variation as in build_array_mesh; floor adds a darkened ground plane
	// at y=0 so the gaps between cells read as alleys instead of holes.
	// height_power as in build_array_mesh.
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
