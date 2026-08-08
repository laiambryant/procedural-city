#ifndef PACKED_SURFACE_H
#define PACKED_SURFACE_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace godot {

class PackedSurface {
public:
	struct Cursor {
		int64_t vertex = 0;
		int64_t index = 0;
	};

	class Writer {
		friend class PackedSurface;

	public:
		void quad(const Vector3 p_pos[4], const Vector2 p_uv[4], const Color p_col[4],
				const Vector3 &p_normal, const Vector3 &p_tangent, float p_tangent_w);
		void fan(const Vector3 *p_pos, const Vector2 *p_uv, int p_corners, const Color &p_col,
				const Vector3 &p_normal, const Vector3 &p_tangent, float p_tangent_w);

	private:
		Writer(PackedSurface &p_surface, const Cursor &p_cursor) :
				surface(p_surface), vi(p_cursor.vertex), ii(p_cursor.index) {}

		int32_t push_vertex(const Vector3 &p_pos, const Vector2 &p_uv, const Color &p_col,
				const Vector3 &p_normal, const Vector3 &p_tangent, float p_tangent_w);
		void push_triangle(int32_t p_a, int32_t p_b, int32_t p_c);

		PackedSurface &surface;
		int64_t vi = 0;
		int64_t ii = 0;
	};

	PackedSurface(int64_t p_vertex_count, int64_t p_index_count, bool p_use_color);

	Writer writer_at(const Cursor &p_cursor) { return Writer(*this, p_cursor); }
	Ref<ArrayMesh> commit();

private:
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedFloat32Array tangents;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
	Vector3 *vp = nullptr;
	Vector3 *np = nullptr;
	float *tp = nullptr;
	Vector2 *uvp = nullptr;
	Color *cp = nullptr;
	int32_t *ip = nullptr;
	bool use_color = false;
};

} // namespace godot

#endif // PACKED_SURFACE_H
